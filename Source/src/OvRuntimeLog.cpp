#include "OvRuntimeLog.h"
#include <mutex>

#if JUCE_WINDOWS
 #include <process.h>
#else
 #include <unistd.h>
#endif

// The whole implementation is compiled out unless the CMake option
// OVERTEON_FILE_LOG is ON, so an OFF build carries no file-system logging code
// and no reference to the user's home directory.
#if OVERTEON_FILE_LOG

namespace
{
    int currentProcessId()
    {
       #if JUCE_WINDOWS
        return int (_getpid());
       #else
        return int (::getpid());
       #endif
    }

    juce::String timestamp()
    {
        const auto now = juce::Time::getCurrentTime();
        return now.formatted ("%Y-%m-%d %H:%M:%S") + "."
               + juce::String (now.getMilliseconds()).paddedLeft ('0', 3);
    }

    std::mutex loggerLock;
    std::unique_ptr<juce::FileLogger> logger;
    juce::Logger* previousLogger = nullptr;
    juce::File logFile;
    int activeUsers = 0;

    /** ~/Overteon/<date>.<time>.log — a name that does not exist yet. */
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

    // Caller holds loggerLock. Only writes while this session's logger is the
    // one installed: a host that swapped in its own logger takes precedence.
    void writeLocked (const char* level, const juce::String& message)
    {
        if (logger == nullptr || juce::Logger::getCurrentLogger() != logger.get())
            return;

        logger->logMessage (timestamp() + "  " + juce::String (level).paddedRight (' ', 5)
                            + "  " + message);
    }
}

#endif

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
    const std::lock_guard<std::mutex> guard (loggerLock);
    return logFile;
   #else
    return {};
   #endif
}

void start()
{
   #if OVERTEON_FILE_LOG
    std::lock_guard<std::mutex> guard (loggerLock);
    if (++activeUsers > 1)
        return;

    logFile = chooseLogFile();
    if (logFile == juce::File() || logFile.create().failed())
    {
        --activeUsers;
        logFile = juce::File();
        DBG ("Overteon: could not create the runtime log under "
             + juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName());
        return;
    }

    // -1: keep the whole file. The name is unique per run, so this constructor
    // can never truncate or append to a previous session's log.
    logger = std::make_unique<juce::FileLogger> (logFile, "Overteon " OVERTEON_VERSION " runtime log", -1);
    previousLogger = juce::Logger::getCurrentLogger();
    juce::Logger::setCurrentLogger (logger.get());

    writeLocked ("INFO", juce::String ("session start: Overteon ") + OVERTEON_VERSION
                         + ", pid " + juce::String (currentProcessId())
                         + ", " + juce::SystemStats::getOperatingSystemName()
                         + ", log " + logFile.getFullPathName());
   #endif
}

void stop()
{
   #if OVERTEON_FILE_LOG
    std::lock_guard<std::mutex> guard (loggerLock);
    if (activeUsers == 0 || --activeUsers > 0)
        return;

    writeLocked ("INFO", "session end");
    if (logger != nullptr)
        juce::Logger::setCurrentLogger (previousLogger);
    logger.reset();
    previousLogger = nullptr;
    logFile = juce::File();
   #endif
}

void info (const juce::String& message)
{
   #if OVERTEON_FILE_LOG
    const std::lock_guard<std::mutex> guard (loggerLock);
    writeLocked ("INFO", message);
   #else
    juce::ignoreUnused (message);
   #endif
}

void warn (const juce::String& message)
{
   #if OVERTEON_FILE_LOG
    const std::lock_guard<std::mutex> guard (loggerLock);
    writeLocked ("WARN", message);
   #else
    juce::ignoreUnused (message);
   #endif
}

void error (const juce::String& message)
{
   #if OVERTEON_FILE_LOG
    const std::lock_guard<std::mutex> guard (loggerLock);
    writeLocked ("ERROR", message);
   #else
    juce::ignoreUnused (message);
   #endif
}
} // namespace ovlog
