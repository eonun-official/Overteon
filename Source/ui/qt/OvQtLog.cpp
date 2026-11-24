#include "OvQtLog.h"
#include "OvRuntimeLog.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFileInfo>
#include <QtCore/QLoggingCategory>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>

#include <juce_core/juce_core.h>

#include <cstdio>

namespace
{
    juce::String toJuce (const QString& text)
    {
        return juce::String (text.toUtf8().constData());
    }

    const char* typeName (QtMsgType type)
    {
        switch (type)
        {
            case QtDebugMsg:    return "debug";
            case QtInfoMsg:     return "info";
            case QtWarningMsg:  return "warning";
            case QtCriticalMsg: return "critical";
            case QtFatalMsg:    return "fatal";
        }
        return "unknown";
    }

    const char* graphicsApiName (QSGRendererInterface::GraphicsApi api)
    {
        using Api = QSGRendererInterface::GraphicsApi;
        switch (api)
        {
            case Api::OpenGL:      return "OpenGL";
            case Api::Vulkan:      return "Vulkan";
            case Api::Metal:       return "Metal";
            case Api::Direct3D11:  return "Direct3D11";
            case Api::Direct3D12:  return "Direct3D12";
            case Api::Software:    return "software";
            case Api::Null:        return "null";
            case Api::OpenVG:      return "OpenVG";
            case Api::Unknown:     return "unknown";
        }
        return "unknown";
    }

    const char* shaderName (QSGRendererInterface::ShaderType type)
    {
        using Lang = QSGRendererInterface::ShaderType;
        switch (type)
        {
            case Lang::GLSL:      return "GLSL";
            case Lang::HLSL:      return "HLSL";
            case Lang::RhiShader: return "RHI shader (QSB)";
            case Lang::UnknownShadingLanguage: return "unknown";
        }
        return "unknown";
    }

    QString shaderFlags (QSGRendererInterface::ShaderCompilationTypes compilation,
                         QSGRendererInterface::ShaderSourceTypes sources)
    {
        using Iface = QSGRendererInterface;
        QStringList flags;
        if (compilation & Iface::RuntimeCompilation) flags << QStringLiteral ("runtime-compiled");
        if (compilation & Iface::OfflineCompilation) flags << QStringLiteral ("offline-compiled");
        if (sources & Iface::ShaderSourceString)     flags << QStringLiteral ("source strings");
        if (sources & Iface::ShaderSourceFile)       flags << QStringLiteral ("source files");
        return flags.join (QStringLiteral (", "));
    }

    // Qt reports a QML compile error as one message containing one line per
    // diagnostic.  Each of those becomes its own timestamped record so that a
    // multi-line error stays readable and greppable.
    void writeMessage (QtMsgType type, const QString& message)
    {
        for (const auto& line : message.split (QLatin1Char ('\n')))
        {
            if (line.trimmed().isEmpty())
                continue;

            if (type == QtDebugMsg || type == QtInfoMsg)
                ovlog::info (toJuce (line));
            else if (type == QtWarningMsg)
                ovlog::warn (toJuce (line));
            else
                ovlog::error (toJuce (line));
        }
    }

    void messageHandler (QtMsgType type, const QMessageLogContext& context, const QString& message)
    {
        if (! ovlog::enabled())
            return;

        QString line = QStringLiteral ("qt(%1)").arg (QString::fromLatin1 (typeName (type)));
        if (context.category != nullptr && qstrcmp (context.category, "default") != 0)
            line += QStringLiteral (" [%1]").arg (QString::fromLatin1 (context.category));
        line += QLatin1Char (' ');
        line += message;

        if (context.file != nullptr)
        {
            QString location = QStringLiteral (" (%1:%2")
                .arg (QFileInfo (QString::fromLatin1 (context.file)).fileName())
                .arg (context.line);
            if (context.function != nullptr)
                location += QStringLiteral (", %1").arg (QString::fromLatin1 (context.function));
            location += QLatin1Char (')');
            line += location;
        }

        writeMessage (type, line);

        // Warnings and worse also stay on stderr: CI logs and a terminal
        // session are the other place a failing UI is diagnosed.
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
            std::fprintf (stderr, "%s\n", line.toUtf8().constData());
    }
}

namespace ovqtlog
{
void install()
{
    if (! ovlog::enabled())
        return;

    static bool installed = false;
    if (installed)
        return;
    installed = true;

    qInstallMessageHandler (messageHandler);

    // Qt filters debug/info out unless a rule says otherwise.  The log build is
    // a diagnostics build, so turn everything on -- and let QT_LOGGING_RULES
    // win when the user set it, because narrowing is the usual reason to set
    // it at all.
    const auto rules = qEnvironmentVariable ("QT_LOGGING_RULES");
    if (! rules.isEmpty())
    {
        ovlog::info ("Qt logging: QT_LOGGING_RULES=" + toJuce (rules));
    }
    else if (! qEnvironmentVariableIsSet ("OVERTEON_QTLOG_VERBOSE")
             && ! qEnvironmentVariableIsSet ("OVERTEON_QTLOG_FULL"))
    {
        // Verbose Qt categories are a diagnosis switch, not a default.  Every
        // Qt debug line goes through the file logger synchronously, and during
        // mouse interaction in a host that is hundreds of lines per second --
        // measurable as the host's GUI stuttering.  Warnings and errors are
        // still recorded; our own lifecycle lines are unaffected.
        QLoggingCategory::setFilterRules (QStringLiteral ("*.debug=false\n*.info=false\n"));
        ovlog::info ("Qt logging: warnings/errors only "
                     "(set OVERTEON_QTLOG_VERBOSE=1 for debug categories)");
    }
    else
    {
        // Default: every Qt category at debug level except the internal traces
        // that fire per item or per frame -- at 60 Hz the font database, the
        // emoji segmenter and the dirty/clip bookkeeping alone produced more
        // than three thousand lines a second, which buries the events a bug
        // report actually needs.  OVERTEON_QTLOG_FULL=1 turns those back on
        // (and QT_LOGGING_RULES overrides both).
        QString rules = QStringLiteral ("*.debug=true\n*.info=true\n");
        const bool full = ! qEnvironmentVariableIsEmpty ("OVERTEON_QTLOG_FULL");
        if (! full)
            rules += QStringLiteral (
                "qt.qpa.xcb.debug=false\n"
                "qt.qpa.wayland.debug=false\n"
                "qt.text.emojisegmenter.debug=false\n"
                "qt.text.font.db.debug=false\n"
                "qt.text.font.cmap.debug=false\n"
                "qt.text.font.match.debug=false\n"
                "qt.quick.effectiveclip.debug=false\n"
                "qt.quick.hover.trace.debug=false\n"
                "qt.quick.dirty.debug=false\n"
                "qt.quick.viewport.debug=false\n"
                "qt.quick.pointer.debug=false\n"
                "qt.quick.text.noderecycling.debug=false\n"
                "qt.qml.overloadresolution.debug=false\n"
                "qt.qml.diskcache.debug=false\n"
                "qt.qml.v4.asm.debug=false\n"
                "qt.qml.delegatemodel.recycling.debug=false\n"
                "qt.qml.gc.allocatorStats.debug=false\n"
                "qt.qml.gc.stepExecution.debug=false\n"
                "qt.scenegraph.renderloop.debug=false\n"
                "qt.scenegraph.time.*=false\n"
                "qt.scenegraph.softwarecontext.*=false\n"
                "qt.quick.hover.cursor.debug=false\n"
                "qt.quick.mouse.debug=false\n");
        QLoggingCategory::setFilterRules (rules);
        ovlog::info (full ? "Qt logging: every category enabled, including per-frame internals "
                            "(OVERTEON_QTLOG_FULL=1)"
                          : "Qt logging: verbose categories enabled; per-frame internals filtered "
                            "(set OVERTEON_QTLOG_FULL=1 for those, or QT_LOGGING_RULES to override)");
    }
}

void logEnvironment()
{
    if (! ovlog::enabled())
        return;

    QStringList lines;
    lines << QStringLiteral ("Qt %1, platform plug-in %2")
                 .arg (QString::fromLatin1 (qVersion()), QGuiApplication::platformName());
    lines << QStringLiteral ("JUCE %1").arg (QString::fromUtf8 (juce::SystemStats::getJUCEVersion().toRawUTF8()));
    lines << QStringLiteral ("OS %1").arg (QString::fromUtf8 (juce::SystemStats::getOperatingSystemName().toRawUTF8()));

    const auto paths = QCoreApplication::libraryPaths();
    if (! paths.isEmpty())
        lines << QStringLiteral ("Qt library paths: %1").arg (paths.join (QStringLiteral (", ")));

    for (auto* screen : QGuiApplication::screens())
    {
        if (screen == nullptr)
            continue;
        const auto geometry = screen->geometry();
        const auto available = screen->availableGeometry();
        lines << QStringLiteral ("screen %1: %2x%3 at %4,%5, available %6x%7, "
                                 "dpr %8, logical DPI %9, refresh %10 Hz")
                     .arg (screen->name())
                     .arg (geometry.width()).arg (geometry.height())
                     .arg (geometry.x()).arg (geometry.y())
                     .arg (available.width()).arg (available.height())
                     .arg (screen->devicePixelRatio())
                     .arg (screen->logicalDotsPerInch())
                     .arg (screen->refreshRate());
    }

    // The switches that change how this UI behaves at run time, so a report of
    // "it looks wrong here" can be reproduced from the log alone.
    for (const char* name : { "QT_QPA_PLATFORM", "QSG_RHI_BACKEND", "QT_QUICK_BACKEND",
                              "QSG_RENDER_LOOP", "QT_OPENGL", "LIBGL_ALWAYS_SOFTWARE",
                              "DISPLAY", "WAYLAND_DISPLAY", "XDG_SESSION_TYPE",
                              "OVERTEON_CPUFROST", "OVERTEON_NOFROST", "OVERTEON_GLASSSIGMA",
                              "OVERTEON_FIXED_SIZE", "OVERTEON_RENDERLOOP",
                              "OVERTEON_SIZEDBG", "OVERTEON_KEYDBG",
                              "OVERTEON_QTLOG_FULL" })
    {
        const auto value = qEnvironmentVariable (name);
        if (! value.isEmpty())
            lines << QStringLiteral ("env %1=%2").arg (QString::fromLatin1 (name), value);
    }

    for (const auto& line : lines)
        ovlog::info (toJuce (line));
}

void logQuickWindow (QQuickWindow* window)
{
    if (! ovlog::enabled() || window == nullptr)
        return;

    const auto* iface = window->rendererInterface();
    if (iface == nullptr)
    {
        ovlog::info ("Qt Quick: no renderer interface");
        return;
    }

    ovlog::info ("Qt Quick: " + juce::String (graphicsApiName (iface->graphicsApi()))
                 + ", shaders " + shaderName (iface->shaderType())
                 + (shaderFlags (iface->shaderCompilationType(), iface->shaderSourceType()).isEmpty()
                        ? juce::String()
                        : " (" + toJuce (shaderFlags (iface->shaderCompilationType(),
                                                      iface->shaderSourceType())) + ")")
                 + ", scenegraph initialised "
                 + (window->isSceneGraphInitialized() ? "yes" : "no"));

    const auto size = window->size();
    ovlog::info ("Qt Quick window: " + juce::String (size.width()) + "x" + juce::String (size.height())
                 + " DIP, dpr " + juce::String (window->devicePixelRatio(), 4)
                 + ", visible " + (window->isVisible() ? "yes" : "no"));
}

void event (const QString& message)
{
    if (! ovlog::enabled())
        return;
    ovlog::info ("ui: " + toJuce (message));
}
} // namespace ovqtlog
