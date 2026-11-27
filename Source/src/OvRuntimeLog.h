#pragma once
#include <juce_core/juce_core.h>

// ===========================================================================
//  Overteon runtime logging system (Qt build)
//
//  Configure with -DOVERTEON_FILE_LOG=ON.  The first Overteon instance created
//  in a process opens
//
//      <user home>/Overteon/<date>.<time>.log
//
//  and every record carries: timestamp (ms), level, thread name, category,
//  message, and optionally source file:line.
//
//  Levels: off < error < warn < info < debug < trace.
//
//  Runtime control (read once at start(), all optional):
//      OVERTEON_LOG_LEVEL=off|error|warn|info|debug|trace   (default: info)
//      OVERTEON_LOG_CATEGORIES=editor,qt,audio              (prefix whitelist)
//      OVERTEON_LOG_ALL=1                                   (every category)
//      OVERTEON_LOG_CRASH_TEST=<seconds>                    (deliberate crash)
//      OVERTEON_LOG_MAX_MB=<n>                              (default 64)
//
//  The writer is asynchronous: log() formats a record and appends it to a
//  bounded queue; a dedicated thread writes and flushes.  A category that
//  produces more than kRecordsPerSecondPerCategory records per second is
//  throttled and the number of suppressed records is reported, so a noisy
//  Qt category cannot turn into host UI lag (the reason the Qt debug
//  categories used to be switched off wholesale).
//
//  Crash safety: SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS (POSIX),
//  SetUnhandledExceptionFilter + std::terminate (Windows) write a banner and a
//  stack trace directly to a pre-opened file descriptor, because the normal
//  writer thread cannot be trusted at that point.
//
//  When the option is OFF every function below compiles to a no-op: no
//  directory is created, no thread is started, and the JUCE current logger is
//  left untouched (a host's own logger keeps working).
// ===========================================================================
namespace ovlog
{
    enum class Level : int
    {
        off = 0,
        error,
        warn,
        info,
        debug,
        trace
    };

    /** True when the build was configured with -DOVERTEON_FILE_LOG=ON. */
    bool enabled();

    /** Reference-counted session log.  The first start() creates the directory
        and file, installs the crash handlers and the JUCE logger; the last
        stop() flushes, restores the previous logger and closes the file. */
    void start();
    void stop();

    /** The file being written, or an invalid File when logging is off
        (or the file could not be created). Safe from any thread. */
    juce::File file();

    /** Current threshold; records below it are dropped in log(). */
    Level level();
    bool categoryEnabled (const juce::String& category);

    /** Write one record.  `sourceFile` may be nullptr. */
    void log (Level level, const juce::String& category, const juce::String& message,
              const char* sourceFile = nullptr, int sourceLine = 0);

    /** Convenience overloads (category defaults to "app"). */
    void trace (const juce::String& category, const juce::String& message,
                const char* sourceFile = nullptr, int sourceLine = 0);
    void debug (const juce::String& category, const juce::String& message,
                const char* sourceFile = nullptr, int sourceLine = 0);
    void info  (const juce::String& category, const juce::String& message,
                const char* sourceFile = nullptr, int sourceLine = 0);
    void warn  (const juce::String& category, const juce::String& message,
                const char* sourceFile = nullptr, int sourceLine = 0);
    void error (const juce::String& category, const juce::String& message,
                const char* sourceFile = nullptr, int sourceLine = 0);

    /** Historical single-argument forms, category "app". */
    void info  (const juce::String& message);
    void warn  (const juce::String& message);
    void error (const juce::String& message);

    /** Flush everything queued so far (blocks until the writer caught up). */
    void flush();

    /** Process-wide counters and a cheap resource sample. */
    struct Diagnostics
    {
        juce::int64 recordsWritten = 0;
        juce::int64 recordsDropped = 0;      // queue overflow / below level
        juce::int64 recordsThrottled = 0;    // per-category rate limit
        double     cpuPercent = -1.0;        // -1 when not available
        double     memoryMB = -1.0;
        int        threadCount = -1;
        double     secondsSinceStart = 0.0;
    };

    Diagnostics diagnostics();

    /** Logs a one-line resource sample (category "perf", level debug).  The
        editor calls this from its timer every few seconds. */
    void logProcessSample();

    /** True when the build has crash handlers installed. */
    bool crashHandlersInstalled();
}
