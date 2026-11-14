# Cross-compile Overteon for Windows from Linux (MinGW-w64).
#
#   cmake -S . -B build-mingw -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
#         -DOVERTEON_QT_UI=ON
#   ninja -C build-mingw
#
# Notes
#  * Qt6 for Windows is required separately: point OVERTEON_MINGW_QT at that
#    installation (it must contain lib/cmake/Qt6).  The native Linux Qt cannot
#    be linked into a PE binary.
#  * The WebView2 editor (OVERTEON_QT_UI=OFF) needs no Qt and cross-compiles
#    with just this file plus the WebView2 SDK that ships in webview2/.
#  * Web browser support is off for the Qt build: JUCE's Linux/Windows
#    WebBrowserComponent drags in the platform SDK, and the Qt editor does not
#    need it.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})

# look for headers/libs in the target tree, programs on the host
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Qt for Windows (the target libraries) and a native Qt of the SAME version
# (the host tools: moc, rcc, uic — CMake runs them on the build machine).
set(OVERTEON_MINGW_QT "$ENV{HOME}/Qt-mingw/6.9.3/mingw_64" CACHE PATH "Qt 6 for Windows/MinGW")
set(OVERTEON_HOST_QT  "$ENV{HOME}/Qt-host/6.9.3/gcc_64"   CACHE PATH "native Qt 6, same version (host tools)")

if(EXISTS "${OVERTEON_MINGW_QT}")
    list(APPEND CMAKE_PREFIX_PATH "${OVERTEON_MINGW_QT}")
    set(CMAKE_FIND_ROOT_PATH ${CMAKE_FIND_ROOT_PATH} "${OVERTEON_MINGW_QT}")
endif()

if(EXISTS "${OVERTEON_HOST_QT}/lib/cmake/Qt6CoreTools")
    # QtConfig looks its *Tools packages up here instead of in the target tree
    set(QT_HOST_PATH "${OVERTEON_HOST_QT}" CACHE PATH "" FORCE)
    set(QT_HOST_PATH_CMAKE_DIR "${OVERTEON_HOST_QT}" CACHE PATH "" FORCE)
endif()

# static-link the MinGW runtime so the DLLs are not needed next to the plugin
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")

# Qt's host tools (moc, rcc, uic, qmlcachegen) are Windows .exe files.  CMake
# runs them during configure/generate, so they have to go through wine.  The
# wrapper scripts in ~/bin-wine/ do exactly that.
set(OVERTEON_WINE_TOOLS "$ENV{HOME}/bin-wine" CACHE PATH "wine wrappers for Qt host tools")
if(EXISTS "${OVERTEON_WINE_TOOLS}/moc")
    set(ACTC_QT_MOC_EXECUTABLE    "${OVERTEON_WINE_TOOLS}/moc" CACHE FILEPATH "" FORCE)
    set(ACTC_QT_RCC_EXECUTABLE    "${OVERTEON_WINE_TOOLS}/rcc" CACHE FILEPATH "" FORCE)
    set(ACTC_QT_UIC_EXECUTABLE    "${OVERTEON_WINE_TOOLS}/uic" CACHE FILEPATH "" FORCE)
endif()
