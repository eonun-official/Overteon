#pragma once

#include <QLibrary>
#include <QtGlobal>

// One policy for both the embedded editor and preview. Qt RHI supplies the
// graphics abstraction; our Slang shaders ship SPIR-V, MSL and HLSL only.
namespace ovgraphics
{
inline bool runningUnderWine()
{
#ifdef Q_OS_WIN
    QLibrary ntdll (QStringLiteral ("ntdll"));
    return ntdll.resolve ("wine_get_version") != nullptr;
#else
    return false;
#endif
}

inline bool hasBackendOverride()
{
    return ! qEnvironmentVariableIsEmpty ("QSG_RHI_BACKEND")
        || ! qEnvironmentVariableIsEmpty ("QT_QUICK_BACKEND");
}

// Call before constructing our application. macOS uses Qt's Metal default and
// native Windows uses D3D. Linux and Wine use Vulkan instead of OpenGL because
// the application does not carry GLSL shader variants. Existing Qt hosts keep
// the backend they already initialized; RHI selection is process-wide.
inline void configureDefaults (bool wine)
{
    if (hasBackendOverride()) return;
#ifdef Q_OS_LINUX
    Q_UNUSED (wine);
    qputenv ("QSG_RHI_BACKEND", "vulkan");
#else
    if (wine) qputenv ("QSG_RHI_BACKEND", "vulkan");
#endif
}
}
