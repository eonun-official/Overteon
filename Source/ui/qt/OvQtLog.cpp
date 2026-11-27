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
    ovlog::Level levelFor (QtMsgType type)
    {
        switch (type)
        {
            case QtDebugMsg:    return ovlog::Level::debug;
            case QtInfoMsg:     return ovlog::Level::info;
            case QtWarningMsg:  return ovlog::Level::warn;
            case QtCriticalMsg: return ovlog::Level::error;
            case QtFatalMsg:    return ovlog::Level::error;
        }
        return ovlog::Level::info;
    }

    void messageHandler (QtMsgType type, const QMessageLogContext& context, const QString& message)
    {
        if (! ovlog::enabled())
            return;

        const auto level = levelFor (type);
        juce::String category ("qt");
        if (context.category != nullptr && qstrcmp (context.category, "default") != 0)
            category = juce::String ("qt/") + juce::String::fromUTF8 (context.category);

        for (const auto& line : message.split (QLatin1Char ('\n')))
        {
            if (line.trimmed().isEmpty())
                continue;
            ovlog::log (level, category, toJuce (line), context.file, context.line);
        }

        // Warnings and worse also stay on stderr: CI logs and a terminal
        // session are the other place a failing UI is diagnosed.
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
            std::fprintf (stderr, "%s\n", message.toUtf8().constData());
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

    // Which Qt categories reach the logger.  The handler itself enforces the
    // log level and the per-category rate limit, so "all categories" is safe
    // for performance now; debug categories are only asked for when the level
    // is debug or the explicit switches are set.
    const auto rules = qEnvironmentVariable ("QT_LOGGING_RULES");
    const bool verbose = int (ovlog::level()) >= int (ovlog::Level::debug)
                         || qEnvironmentVariableIsSet ("OVERTEON_QTLOG_VERBOSE")
                         || qEnvironmentVariableIsSet ("OVERTEON_QTLOG_FULL");

    if (! rules.isEmpty())
    {
        QLoggingCategory::setFilterRules (rules);
        ovlog::info ("qt", "logging: QT_LOGGING_RULES=" + toJuce (rules));
    }
    else if (! verbose)
    {
        QLoggingCategory::setFilterRules (QStringLiteral ("*.debug=false"));
        ovlog::info ("qt", "logging: info/warn/error only "
                           "(OVERTEON_LOG_LEVEL=debug or OVERTEON_QTLOG_VERBOSE=1 "
                           "for Qt debug categories)");
    }
    else
    {
        // Every category, minus the internals that fire per item or per frame.
        // At 60 Hz the font database, the emoji segmenter and the dirty/clip
        // bookkeeping alone produce thousands of records a second; they are
        // still available with OVERTEON_QTLOG_FULL=1 or QT_LOGGING_RULES.
        QString ruleText = QStringLiteral ("*.debug=true\n*.info=true\n");
        const bool full = ! qEnvironmentVariableIsEmpty ("OVERTEON_QTLOG_FULL");
        if (! full)
            ruleText += QStringLiteral (
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
        QLoggingCategory::setFilterRules (ruleText);
        ovlog::info ("qt", full
            ? "logging: every category enabled, including per-frame internals"
            : "logging: your categories at debug; per-frame internals filtered "
              "(OVERTEON_QTLOG_FULL=1 for those)");
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
        ovlog::info ("qt", toJuce (line));
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
