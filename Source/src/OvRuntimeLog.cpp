#include "OvRuntimeLog.h"

#include <atomic>
#include <exception>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <process.h>
 #include <tlhelp32.h>
 #include <psapi.h>
 #pragma comment(lib, "psapi.lib")
#else
 #include <dirent.h>
 #include <pthread.h>
 #include <signal.h>
 #include <sys/resource.h>
 #include <sys/stat.h>
 #include <fcntl.h>
 #include <unistd.h>
 #if JUCE_MAC
  #include <mach/mach.h>
 #else
  #include <sys/syscall.h>
 #endif
#endif

// The whole implementation is compiled out unless the CMake option
// OVERTEON_FILE_LOG is ON, so an OFF build carries no file-system logging code
// and no reference to the user's home directory.
#if OVERTEON_FILE_LOG

namespace
{
    // -----------------------------------------------------------------------
    //  configuration
    // -----------------------------------------------------------------------
    constexpr int    kQueueLimit            = 200000;   // pending records
    constexpr int    kRecordsPerSecond      = 400;      // per category
    constexpr int    kWriterBatch           = 512;
    constexpr int    kFlushIntervalMs       = 250;
    constexpr juce::int64 kDefaultMaxBytes  = 64 * 1024 * 1024;

    std::atomic<bool> started { false };
    int activeUsers = 0;
    std::atomic<ovlog::Level> threshold { ovlog::Level::info };
    std::atomic<bool> allCategories { false };
    std::atomic<juce::int64> written { 0 }, dropped { 0 }, throttled { 0 };
    std::atomic<juce::int64> maxBytes { kDefaultMaxBytes };
    std::atomic<bool> crashHandlers { false };
    std::atomic<bool> sizeCapLogged { false };

    juce::CriticalSection stateLock;
    juce::File logFile;
    juce::FileOutputStream* stream = nullptr;
    juce::String categoryFilter;                  // comma separated prefixes
    std::map<juce::String, juce::int64> categoryWindows;   // window start ms
    std::map<juce::String, int> categoryCounts;            // records in window
    std::deque<juce::String> queue;
    std::unique_ptr<juce::Thread> writer;
    juce::WaitableEvent wakeUp;
    juce::int64 startedAtMs = 0;
    juce::int64 bytesWritten = 0;

   #if JUCE_WINDOWS
    HANDLE crashHandle = INVALID_HANDLE_VALUE;
   #else
    int crashFd = -1;
   #endif

   #if JUCE_LINUX || JUCE_BSD
    extern "C" int pthread_getname_np (pthread_t, char*, size_t);
   #endif

    // -----------------------------------------------------------------------
    //  helpers
    // -----------------------------------------------------------------------
    int currentProcessId()
    {
       #if JUCE_WINDOWS
        return int (_getpid());
       #else
        return int (::getpid());
       #endif
    }

    juce::int64 nowMs()
    {
        return juce::Time::currentTimeMillis();
    }

    juce::String timestamp()
    {
        const auto now = juce::Time::getCurrentTime();
        return now.formatted ("%Y-%m-%d %H:%M:%S") + "."
               + juce::String (now.getMilliseconds()).paddedLeft ('0', 3);
    }

    const char* levelTag (ovlog::Level level)
    {
        switch (level)
        {
            case ovlog::Level::error: return "ERROR";
            case ovlog::Level::warn:  return "WARN ";
            case ovlog::Level::info:  return "INFO ";
            case ovlog::Level::debug: return "DEBUG";
            case ovlog::Level::trace: return "TRACE";
            case ovlog::Level::off:   break;
        }
        return "OFF  ";
    }

    ovlog::Level parseLevel (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        if (t == "off" || t == "none" || t == "0") return ovlog::Level::off;
        if (t == "error")  return ovlog::Level::error;
        if (t == "warn" || t == "warning") return ovlog::Level::warn;
        if (t == "info")   return ovlog::Level::info;
        if (t == "debug")  return ovlog::Level::debug;
        if (t == "trace")  return ovlog::Level::trace;
        return ovlog::Level::info;
    }

    juce::String threadLabel()
    {
        static thread_local juce::String label;
        if (label.isNotEmpty())
            return label;

       #if JUCE_WINDOWS
        const auto tid = int (GetCurrentThreadId());
       #elif JUCE_MAC
        const auto tid = int (pthread_mach_thread_np (pthread_self()));
       #else
        const auto tid = int (::syscall (SYS_gettid));
       #endif

        juce::String name;
       #if JUCE_LINUX || JUCE_BSD
        char buffer[32] = {};
        if (pthread_getname_np (pthread_self(), buffer, sizeof (buffer)) == 0)
            name = juce::String (buffer).trim();
       #endif

        label = name.isNotEmpty() ? name + "/" + juce::String (tid)
                                  : "tid-" + juce::String (tid);
        return label;
    }

    juce::String sourceTag (const char* file, int line)
    {
        if (file == nullptr || *file == 0)
            return {};
        juce::String name (file);
        const auto slash = name.lastIndexOfAnyOf ("/\\");
        if (slash >= 0)
            name = name.substring (slash + 1);
        return line > 0 ? "  (" + name + ":" + juce::String (line) + ")"
                        : "  (" + name + ")";
    }

    bool categoryAllowedLocked (const juce::String& category)
    {
        if (allCategories.load() || categoryFilter.isEmpty())
            return true;
        for (const auto& prefix : juce::StringArray::fromTokens (categoryFilter, ",", ""))
            if (prefix.isNotEmpty() && category.startsWith (prefix))
                return true;
        return false;
    }

    // Token bucket per category: stops a chatty Qt category from filling the
    // queue (and the disk) while still reporting that it was suppressed.
    bool rateAllowedLocked (const juce::String& category, juce::int64 now)
    {
        auto& windowStart = categoryWindows[category];
        int& count = categoryCounts[category];
        if (windowStart == 0 || now - windowStart >= 1000)
        {
            if (count > kRecordsPerSecond)
                throttled.fetch_add (count - kRecordsPerSecond);
            windowStart = now;
            count = 0;
        }
        ++count;
        if (count > kRecordsPerSecond)
        {
            throttled.fetch_add (1);
            return false;
        }
        return true;
    }

    void writeCrashText (const char* text, size_t length)
    {
       #if JUCE_WINDOWS
        if (crashHandle != INVALID_HANDLE_VALUE)
        {
            DWORD done = 0;
            WriteFile (crashHandle, text, DWORD (length), &done, nullptr);
        }
       #else
        if (crashFd >= 0)
            (void) ::write (crashFd, text, length);
       #endif
    }

    void flushCrashFile()
    {
       #if JUCE_WINDOWS
        if (crashHandle != INVALID_HANDLE_VALUE)
            FlushFileBuffers (crashHandle);
       #else
        if (crashFd >= 0)
            (void) ::fsync (crashFd);
       #endif
    }

    // -----------------------------------------------------------------------
    //  crash handlers
    // -----------------------------------------------------------------------
   #if JUCE_WINDOWS
    LONG WINAPI unhandledExceptionFilter (EXCEPTION_POINTERS* info)
    {
        const auto code = info != nullptr && info->ExceptionRecord != nullptr
                            ? info->ExceptionRecord->ExceptionCode : 0u;
        const auto address = info != nullptr && info->ExceptionRecord != nullptr
                            ? reinterpret_cast<juce::pointer_sized_int> (info->ExceptionRecord->ExceptionAddress)
                            : 0;
        char banner[256];
        std::snprintf (banner, sizeof (banner),
                       "\n*** OVERTEON CRASH *** code=0x%08lX address=0x%llX thread=%lu\n",
                       static_cast<unsigned long> (code),
                       static_cast<unsigned long long> (address),
                       static_cast<unsigned long> (GetCurrentThreadId()));
        writeCrashText (banner, std::strlen (banner));
        const auto backtrace = juce::SystemStats::getStackBacktrace();
        writeCrashText (backtrace.toRawUTF8(), std::strlen (backtrace.toRawUTF8()));
        writeCrashText ("\n*** END CRASH ***\n", std::strlen ("\n*** END CRASH ***\n"));
        flushCrashFile();
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void terminateHandler()
    {
        const char* banner = "\n*** OVERTEON std::terminate ***\n";
        writeCrashText (banner, std::strlen (banner));
        const auto backtrace = juce::SystemStats::getStackBacktrace();
        writeCrashText (backtrace.toRawUTF8(), std::strlen (backtrace.toRawUTF8()));
        writeCrashText ("\n*** END TERMINATE ***\n", std::strlen ("\n*** END TERMINATE ***\n"));
        flushCrashFile();
        std::abort();
    }
   #else
    void crashSignalHandler (int signalNumber)
    {
        char banner[256];
        std::snprintf (banner, sizeof (banner),
                       "\n*** OVERTEON CRASH *** signal=%d (%s) pid=%d\n",
                       signalNumber,
                       signalNumber == SIGSEGV ? "SIGSEGV"
                     : signalNumber == SIGABRT ? "SIGABRT"
                     : signalNumber == SIGFPE  ? "SIGFPE"
                     : signalNumber == SIGILL  ? "SIGILL"
                     : signalNumber == SIGBUS  ? "SIGBUS" : "?",
                       currentProcessId());
        writeCrashText (banner, std::strlen (banner));

        // getStackBacktrace is not async-signal-safe, but it is the difference
        // between "a crash happened" and "a crash happened here"; it is used
        // only as a last resort before the process dies.
        const auto backtrace = juce::SystemStats::getStackBacktrace();
        writeCrashText (backtrace.toRawUTF8(), std::strlen (backtrace.toRawUTF8()));
        const char* footer = "\n*** END CRASH ***\n";
        writeCrashText (footer, std::strlen (footer));
        flushCrashFile();

        ::signal (signalNumber, SIG_DFL);
        ::raise (signalNumber);
    }

    void terminateHandler()
    {
        const char* banner = "\n*** OVERTEON std::terminate ***\n";
        writeCrashText (banner, std::strlen (banner));
        const auto backtrace = juce::SystemStats::getStackBacktrace();
        writeCrashText (backtrace.toRawUTF8(), std::strlen (backtrace.toRawUTF8()));
        flushCrashFile();
        std::abort();
    }
   #endif

    void installCrashHandlers()
    {
        if (crashHandlers.exchange (true))
            return;
       #if JUCE_WINDOWS
        SetUnhandledExceptionFilter (unhandledExceptionFilter);
       #else
        for (int sig : { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS })
        {
            struct sigaction action = {};
            action.sa_handler = crashSignalHandler;
            sigemptyset (&action.sa_mask);
            action.sa_flags = SA_RESETHAND;
            sigaction (sig, &action, nullptr);
        }
       #endif
        std::set_terminate (terminateHandler);
    }

    // -----------------------------------------------------------------------
    //  the asynchronous writer
    // -----------------------------------------------------------------------
    class WriterThread final : public juce::Thread
    {
    public:
        WriterThread() : juce::Thread ("OverteonLogWriter") {}

        void run() override
        {
            juce::int64 lastFlush = nowMs();

            while (! threadShouldExit())
            {
                wakeUp.wait (kFlushIntervalMs);

                std::vector<juce::String> batch;
                {
                    const juce::ScopedLock lock (stateLock);
                    while (! queue.empty() && int (batch.size()) < kWriterBatch)
                    {
                        batch.push_back (std::move (queue.front()));
                        queue.pop_front();
                    }
                }

                bool flushNow = batch.empty() && nowMs() - lastFlush >= kFlushIntervalMs;

                for (const auto& line : batch)
                {
                    if (stream == nullptr)
                        break;

                    const auto bytes = line.getNumBytesAsUTF8() + 1;
                    if (bytesWritten + bytes > maxBytes.load())
                    {
                        if (! sizeCapLogged.exchange (true))
                        {
                            const auto note = timestamp()
                                + "  WARN   [log] log size cap reached ("
                                + juce::String (maxBytes.load() / (1024 * 1024))
                                + " MB); further records are dropped\n";
                            stream->write (note.toRawUTF8(), std::strlen (note.toRawUTF8()));
                            stream->flush();
                        }
                        dropped.fetch_add (1);
                        continue;
                    }

                    stream->write (line.toRawUTF8(), line.getNumBytesAsUTF8());
                    stream->write ("\n", 1);
                    bytesWritten += bytes;
                    written.fetch_add (1);
                    if (line.contains ("  ERROR ") || line.contains ("  WARN  "))
                        flushNow = true;
                }

                if (flushNow && stream != nullptr)
                {
                    stream->flush();
                    lastFlush = nowMs();
                }
            }

            // final drain
            for (;;)
            {
                juce::String line;
                {
                    const juce::ScopedLock lock (stateLock);
                    if (queue.empty())
                        break;
                    line = std::move (queue.front());
                    queue.pop_front();
                }
                if (stream != nullptr)
                {
                    stream->write (line.toRawUTF8(), line.getNumBytesAsUTF8());
                    stream->write ("\n", 1);
                    bytesWritten += line.getNumBytesAsUTF8() + 1;
                    written.fetch_add (1);
                }
            }
            if (stream != nullptr)
                stream->flush();
        }
    };

    class JuceBridgeLogger final : public juce::Logger
    {
    public:
        void logMessage (const juce::String& message) override
        {
            ovlog::log (ovlog::Level::info, "juce", message);
        }
    };

    JuceBridgeLogger juceBridge;
    juce::Logger* previousLogger = nullptr;

    // -----------------------------------------------------------------------
    //  process sampling
    // -----------------------------------------------------------------------
    struct PreviousSample
    {
        double cpuSeconds = 0.0;
        juce::int64 atMs = 0;
    };
    PreviousSample previousSample;

    double processMemoryMB()
    {
       #if JUCE_WINDOWS
        PROCESS_MEMORY_COUNTERS_EX counters = {};
        if (GetProcessMemoryInfo (GetCurrentProcess(),
                                  reinterpret_cast<PROCESS_MEMORY_COUNTERS*> (&counters),
                                  sizeof (counters)))
            return double (counters.WorkingSetSize) / (1024.0 * 1024.0);
        return -1.0;
       #else
        if (FILE* f = std::fopen ("/proc/self/statm", "r"))
        {
            long pagesTotal = 0, pagesResident = 0;
            const bool ok = std::fscanf (f, "%ld %ld", &pagesTotal, &pagesResident) == 2;
            std::fclose (f);
            if (ok)
                return double (pagesResident) * double (sysconf (_SC_PAGESIZE)) / (1024.0 * 1024.0);
        }
        return -1.0;
       #endif
    }

    double processCpuSeconds()
    {
       #if JUCE_WINDOWS
        FILETIME creation {}, exitTime {}, kernel {}, user {};
        if (GetProcessTimes (GetCurrentProcess(), &creation, &exitTime, &kernel, &user))
        {
            auto toSeconds = [] (const FILETIME& t)
            {
                ULARGE_INTEGER v;
                v.LowPart = t.dwLowDateTime;
                v.HighPart = t.dwHighDateTime;
                return double (v.QuadPart) / 1.0e7;
            };
            return toSeconds (kernel) + toSeconds (user);
        }
        return -1.0;
       #else
        struct rusage usage = {};
        if (getrusage (RUSAGE_SELF, &usage) == 0)
            return double (usage.ru_utime.tv_sec) + double (usage.ru_utime.tv_usec) / 1.0e6
                 + double (usage.ru_stime.tv_sec) + double (usage.ru_stime.tv_usec) / 1.0e6;
        return -1.0;
       #endif
    }

    int processThreadCount()
    {
       #if JUCE_WINDOWS
        int count = 0;
        if (auto snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0); snapshot != INVALID_HANDLE_VALUE)
        {
            THREADENTRY32 entry = {};
            entry.dwSize = sizeof (entry);
            const auto pid = GetCurrentProcessId();
            if (Thread32First (snapshot, &entry))
                do { if (entry.th32OwnerProcessID == pid) ++count; }
                while (Thread32Next (snapshot, &entry));
            CloseHandle (snapshot);
        }
        return count;
       #else
        DIR* dir = opendir ("/proc/self/task");
        if (dir == nullptr)
            return -1;
        int count = 0;
        while (auto* entry = readdir (dir))
            if (entry->d_name[0] != '.')
                ++count;
        closedir (dir);
        return count;
       #endif
    }

    juce::File chooseLogFile()
    {
        const auto directory = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                   .getChildFile ("Overteon");
        if (! directory.createDirectory())
            return {};

        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d.%H-%M-%S");
        for (int attempt = 0; attempt < 1000; ++attempt)
        {
            auto candidate = attempt == 0
                ? directory.getChildFile (stamp + ".log")
                : directory.getChildFile (stamp + "-" + juce::String (attempt + 1) + ".log");
            if (! candidate.existsAsFile())
                return candidate;
        }
        return {};
    }

    void openCrashFile (const juce::File& file)
    {
       #if JUCE_WINDOWS
        crashHandle = CreateFileW (file.getFullPathName().toWideCharPointer(),
                                   FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
       #else
        crashFd = ::open (file.getFullPathName().toRawUTF8(),
                          O_WRONLY | O_APPEND | O_CREAT, 0644);
       #endif
    }

    void closeCrashFile()
    {
       #if JUCE_WINDOWS
        if (crashHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle (crashHandle);
            crashHandle = INVALID_HANDLE_VALUE;
        }
       #else
        if (crashFd >= 0)
        {
            ::close (crashFd);
            crashFd = -1;
        }
       #endif
    }
}

#endif  // OVERTEON_FILE_LOG

namespace ovlog
{

bool enabled()
{
   #if OVERTEON_FILE_LOG
    return true;
   #else
    return false;
   #endif
}

juce::File file()
{
   #if OVERTEON_FILE_LOG
    const juce::ScopedLock lock (stateLock);
    return logFile;
   #else
    return {};
   #endif
}

Level level()
{
   #if OVERTEON_FILE_LOG
    return threshold.load();
   #else
    return Level::off;
   #endif
}

bool categoryEnabled (const juce::String& category)
{
   #if OVERTEON_FILE_LOG
    const juce::ScopedLock lock (stateLock);
    return categoryAllowedLocked (category);
   #else
    juce::ignoreUnused (category);
    return false;
   #endif
}

void log (Level lev, const juce::String& category, const juce::String& message,
          const char* sourceFile, int sourceLine)
{
   #if OVERTEON_FILE_LOG
    if (! started.load() || lev == Level::off)
        return;
    if (int (lev) > int (threshold.load()))
    {
        dropped.fetch_add (1);
        return;
    }

    const auto now = nowMs();

    juce::String line;
    {
        const juce::ScopedLock lock (stateLock);
        if (! categoryAllowedLocked (category))
        {
            dropped.fetch_add (1);
            return;
        }
        if (! rateAllowedLocked (category, now))
            return;

        line = timestamp() + "  " + levelTag (lev) + "  [" + threadLabel() + "] "
             + category + ": " + message + sourceTag (sourceFile, sourceLine);

        if (int (queue.size()) >= kQueueLimit)
        {
            queue.pop_front();
            dropped.fetch_add (1);
        }
        queue.push_back (line);
    }
    wakeUp.signal();
   #else
    juce::ignoreUnused (lev, category, message, sourceFile, sourceLine);
   #endif
}

void trace (const juce::String& c, const juce::String& m, const char* f, int l) { log (Level::trace, c, m, f, l); }
void debug (const juce::String& c, const juce::String& m, const char* f, int l) { log (Level::debug, c, m, f, l); }
void info  (const juce::String& c, const juce::String& m, const char* f, int l) { log (Level::info,  c, m, f, l); }
void warn  (const juce::String& c, const juce::String& m, const char* f, int l) { log (Level::warn,  c, m, f, l); }
void error (const juce::String& c, const juce::String& m, const char* f, int l) { log (Level::error, c, m, f, l); }

void info  (const juce::String& message) { info  ("app", message); }
void warn  (const juce::String& message) { warn  ("app", message); }
void error (const juce::String& message) { error ("app", message); }

void flush()
{
   #if OVERTEON_FILE_LOG
    if (! started.load())
        return;
    wakeUp.signal();
    // Wait for the queue to drain; the writer flushes on an empty queue.
    for (int i = 0; i < 500; ++i)
    {
        {
            const juce::ScopedLock lock (stateLock);
            if (queue.empty())
                break;
        }
        juce::Thread::sleep (2);
    }
    wakeUp.signal();
   #endif
}

Diagnostics diagnostics()
{
    Diagnostics d;
   #if OVERTEON_FILE_LOG
    d.recordsWritten = written.load();
    d.recordsDropped = dropped.load();
    d.recordsThrottled = throttled.load();
    d.memoryMB = processMemoryMB();
    d.threadCount = processThreadCount();
    if (started.load())
    {
        const auto cpu = processCpuSeconds();
        const auto seconds = double (nowMs() - startedAtMs) / 1000.0;
        d.secondsSinceStart = seconds > 0.0 ? seconds : 0.0;
        if (cpu >= 0.0 && seconds > 0.5)
            d.cpuPercent = 100.0 * cpu / seconds;
    }
   #endif
    return d;
}

void logProcessSample()
{
   #if OVERTEON_FILE_LOG
    if (! started.load() || int (level()) < int (Level::debug))
        return;

    const auto d = diagnostics();
    juce::String message;
    message << "cpu=" << juce::String (d.cpuPercent, 1) << "%"
            << " mem=" << juce::String (d.memoryMB, 1) << "MB"
            << " threads=" << juce::String (d.threadCount)
            << " uptime=" << juce::String (d.secondsSinceStart, 1) << "s"
            << " lines=" << juce::String (d.recordsWritten)
            << " dropped=" << juce::String (d.recordsDropped)
            << " throttled=" << juce::String (d.recordsThrottled);
    log (Level::debug, "perf", message);
   #endif
}

bool crashHandlersInstalled()
{
   #if OVERTEON_FILE_LOG
    return crashHandlers.load();
   #else
    return false;
   #endif
}

void start()
{
   #if OVERTEON_FILE_LOG
    const juce::ScopedLock lock (stateLock);
    if (++activeUsers > 1)
        return;

    startedAtMs = nowMs();
    threshold = parseLevel (juce::SystemStats::getEnvironmentVariable ("OVERTEON_LOG_LEVEL", "info"));
    categoryFilter = juce::SystemStats::getEnvironmentVariable ("OVERTEON_LOG_CATEGORIES", "").trim();
    allCategories = juce::SystemStats::getEnvironmentVariable ("OVERTEON_LOG_ALL", "0") == "1";
    {
        const auto mb = juce::SystemStats::getEnvironmentVariable ("OVERTEON_LOG_MAX_MB", "64")
                            .getIntValue();
        if (mb > 0)
            maxBytes = juce::int64 (mb) * 1024 * 1024;
    }

    logFile = chooseLogFile();
    if (logFile == juce::File() || logFile.create().failed())
    {
        --activeUsers;
        logFile = juce::File();
        return;
    }

    stream = new juce::FileOutputStream (logFile);
    if (stream == nullptr || ! stream->openedOk())
    {
        delete stream;
        stream = nullptr;
        --activeUsers;
        logFile = juce::File();
        return;
    }

    openCrashFile (logFile);
    installCrashHandlers();

    written = 0;
    dropped = 0;
    throttled = 0;
    bytesWritten = 0;
    sizeCapLogged = false;
    started = true;

    writer = std::make_unique<WriterThread>();
    writer->startThread();

    previousLogger = juce::Logger::getCurrentLogger();
    juce::Logger::setCurrentLogger (&juceBridge);

    const auto d = diagnostics();
    log (Level::info, "log",
         "logging started: version=" OVERTEON_VERSION
         " level=" + juce::String (levelTag (threshold.load())).trim()
         + " pid=" + juce::String (currentProcessId())
         + " file=" + logFile.getFullPathName()
         + " queue=" + juce::String (kQueueLimit)
         + " rate=" + juce::String (kRecordsPerSecond) + "/s/category");
    log (Level::info, "log",
         "build: " + juce::String (__DATE__) + " " + juce::String (__TIME__)
         + ", cpus=" + juce::String (juce::SystemStats::getNumCpus())
         + ", mem=" + juce::String (d.memoryMB, 1) + "MB"
         + ", threads=" + juce::String (d.threadCount)
         + ", os=" + juce::SystemStats::getOperatingSystemName());
    log (Level::info, "log",
         "process: exe=" + juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                              .getFullPathName()
         + " cwd=" + juce::File::getCurrentWorkingDirectory().getFullPathName()
         + " user=" + juce::SystemStats::getLogonName());
    log (Level::debug, "log", "categories: " + (categoryFilter.isEmpty()
                            ? juce::String ("all") : categoryFilter));
   #endif
}

void stop()
{
   #if OVERTEON_FILE_LOG
    {
        const juce::ScopedLock lock (stateLock);
        if (activeUsers == 0 || --activeUsers > 0)
            return;
        if (! started.load())
            return;

        const auto d = diagnostics();
        log (Level::info, "log",
             "logging stopped: lines=" + juce::String (d.recordsWritten)
             + " dropped=" + juce::String (d.recordsDropped)
             + " throttled=" + juce::String (d.recordsThrottled)
             + " bytes=" + juce::String (bytesWritten)
             + " uptime=" + juce::String (d.secondsSinceStart, 1) + "s");

        started = false;
        if (writer != nullptr)
        {
            writer->signalThreadShouldExit();
            wakeUp.signal();
        }
    }

    if (writer != nullptr)
    {
        writer->waitForThreadToExit (4000);
        writer.reset();
    }

    {
        const juce::ScopedLock lock (stateLock);
        delete stream;
        stream = nullptr;
        closeCrashFile();
        logFile = juce::File();
    }

    juce::Logger::setCurrentLogger (previousLogger);
    previousLogger = nullptr;
   #endif
}

} // namespace ovlog
