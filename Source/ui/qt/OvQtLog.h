#pragma once
#include <QtCore/QString>

class QQuickWindow;

// ===========================================================================
//  Qt diagnostics into the Overteon runtime log
//
//  The Qt editor's own messages (qDebug/qInfo/qWarning/qCritical/qFatal, the
//  QML engine's warnings and the qt.qpa / qt.scenegraph categories) go to
//  stderr, which only exists while a terminal is attached. With
//  -DOVERTEON_FILE_LOG=ON this module forwards them to the same
//  ~/Overteon/<date>.<time>.log file as the plug-in's own lifecycle lines, and
//  records the environment the UI is running in (Qt and JUCE versions, platform
//  plug-in, screens, RHI backend, relevant environment switches).
//
//  Everything here is a no-op when the log is not compiled in, so an OFF build
//  keeps Qt's default message handling and logging categories untouched.
// ===========================================================================
namespace ovqtlog
{
    /** Installs the Qt message handler and enables verbose Qt categories.
        Idempotent; call once, after QGuiApplication exists and before the QML
        engine is created. */
    void install();

    /** Qt/JUCE/OS versions, platform plugin, screens, RHI/environment switches. */
    void logEnvironment();

    /** The Qt Quick graphics API and shader configuration of `window`.
        Call from the sceneGraphInitialized signal (queued to the GUI thread). */
    void logQuickWindow (QQuickWindow* window);

    /** One diagnostic line prefixed with "ui: " (embedding, resize, presets). */
    void event (const QString& message);
}
