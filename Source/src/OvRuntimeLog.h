#pragma once
#include <juce_core/juce_core.h>

// ===========================================================================
//  Optional runtime file log (Qt build)
//
//  Configure with -DOVERTEON_FILE_LOG=ON. When enabled, the first Overteon
//  instance created in a process opens
//
//      <user home>/Overteon/<date>.<time>.log
//
//  e.g. ~/Overteon/2026-11-23.19-26-25.log
//
//  and writes a timestamped line for every lifecycle step (instance creation,
//  audio preparation, Qt/QML editor start-up, QML errors, editor open/close).
//  The file is shared by every instance in the process and is closed when the
//  last one is destroyed. Each process run gets its own file, so two sessions
//  never interleave and an old log is never overwritten.
//
//  When the option is OFF every function below compiles to a no-op: no
//  directory is created, no file is written, and the JUCE current logger is
//  left untouched (a host's own logger keeps working).
// ===========================================================================
namespace ovlog
{
    /** True when the build was configured with -DOVERTEON_FILE_LOG=ON. */
    bool enabled();

    /** Reference-counted session log.  The first start() creates the directory
        and file and installs a logger; the last stop() restores the logger that
        was active before and closes the file. */
    void start();
    void stop();

    /** The file being written, or an invalid File when logging is off
        (or the file could not be created). Safe from any thread. */
    juce::File file();

    /** Timestamped messages. Logging when no session is active is ignored. */
    void info  (const juce::String& message);
    void warn  (const juce::String& message);
    void error (const juce::String& message);
}
