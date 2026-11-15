# Cross-compile Overteon for Windows from Linux with clang-cl + lld-link,
# using the MSVC SDK headers and import libraries fetched by xwin.
#
#   cargo install xwin
#   xwin --accept-license splat --output ~/.xwin-cache
# Cross-compiling the full plug-in (not just the preview) also needs three
# things beyond this file:
#
#   * the vendored JUCE patched so its nested vst3_helper build is told to
#     cross-compile -- see cmake/patches/juce-vst3-helper-cross.patch;
#   * WINEPATH pointing at the Qt bin directory when running ninja, so the
#     helper binary can load Qt6Core.dll under wine:
#         WINEPATH=$HOME/Qt-msvc/6.9.3/msvc2022_64/bin ninja -C build-win
#   * case-insensitive aliases for a few SDK headers and import libraries
#     (Dbghelp.h, DbgHelp.lib, Dwrite.lib, ...): Windows resolves these
#     case-insensitively, a Linux filesystem does not.
#
#   # everything (LV2 + VST3 + VST2 + Standalone)
#   cmake -S . -B build-win -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-clangcl-xwin.cmake \
#         -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_PREFIX_PATH=$HOME/Qt-msvc/6.9.3/msvc2022_64
#   WINEPATH=$HOME/Qt-msvc/6.9.3/msvc2022_64/bin ninja -C build-win
#
#   # the host-free viewer only (no JUCE, no wine, no helper)
#   cmake -S . -B build-win -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-clangcl-xwin.cmake \
#         -DOVERTEON_PREVIEW_ONLY=ON
#
# This produces a real PE32+ binary that links against the MSVC CRT, which is
# what wine, the MSVC-built Qt, and any Windows host expect.  No MinGW.
#
# Qt for Windows must be an MSVC build of the same major.minor:
#   ~/Qt-msvc/6.9.3/msvc2022_64
# and a native Qt of that version provides the host tools (moc, rcc).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_CROSSCOMPILING TRUE)

set(_xwin "$ENV{HOME}/.xwin-cache" CACHE PATH "xwin SDK root")
set(_crt  "${_xwin}/crt")
set(_sdk  "${_xwin}/sdk")

if(NOT EXISTS "${_crt}/lib/x86_64" OR NOT EXISTS "${_sdk}/lib")
    message(FATAL_ERROR
        "xwin SDK not found in ${_xwin}.\n"
        "Run:  xwin --accept-license splat --output ${_xwin}")
endif()

set(CMAKE_C_COMPILER   clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER       lld-link)
set(CMAKE_AR           llvm-lib)
set(CMAKE_MT           llvm-rc)

# clang-cl speaks the MSVC command line, so the target has to be set here
set(CMAKE_C_FLAGS_INIT   "--target=x86_64-pc-windows-msvc -fms-compatibility")
set(CMAKE_CXX_FLAGS_INIT "--target=x86_64-pc-windows-msvc -fms-compatibility -fms-extensions")

# The MSVC CRT needs the SDK's include order: UCRT first, then shared/um, then
# the C++ runtime headers from the crt package.
set(_includes
    "/imsvc${_crt}/include"
    "/imsvc${_sdk}/include/ucrt"
    "/imsvc${_sdk}/include/shared"
    "/imsvc${_sdk}/include/um"
    "/imsvc${_sdk}/include/winrt")

string(REPLACE ";" " " _include_flags "${_includes}")
set(CMAKE_C_FLAGS_INIT   "${CMAKE_C_FLAGS_INIT} ${_include_flags}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} ${_include_flags}")

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "/libpath:${_crt}/lib/x86_64 /libpath:${_sdk}/lib/um/x86_64 /libpath:${_sdk}/lib/ucrt/x86_64")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT}")

# The resource compiler needs the same include path as the compiler: JUCE emits
# a .rc that does `#include <windows.h>`, and llvm-rc otherwise reports
# "'windows.h' file not found" and the whole plug-in fails to link.
string(REPLACE "/imsvc" "/I" _rc_includes "${_includes}")
string(REPLACE ";" " " _rc_flags "${_rc_includes}")
set(CMAKE_RC_FLAGS_INIT "${_rc_flags}")

# Programs built for the target cannot run on the build host.  JUCE runs two of
# them while building: juceaide (configure time, already forced to the host via
# JUCE_BUILD_EXTRAS) and juce_vst3_helper (build time, to write
# moduleinfo.json).  Wine is the only way to execute the latter here.  Set
# OVERTEON_NO_WINE=ON to skip VST3 and build only the formats that do not need
# a helper binary.
if(NOT DEFINED OVERTEON_NO_WINE)
    find_program(_ov_wine NAMES wine wine64)
    if(_ov_wine)
        set(CMAKE_CROSSCOMPILING_EMULATOR "${_ov_wine}")
    endif()
endif()

# find headers/libs only in the target tree; run programs on the host
set(CMAKE_FIND_ROOT_PATH "${_sdk}" "${_crt}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Qt for Windows (MSVC build) and a native Qt of the same version for the host
# tools.  CMake runs moc/rcc on the build machine.
set(OVERTEON_MSVC_QT "$ENV{HOME}/Qt-msvc/6.9.3/msvc2022_64" CACHE PATH "Qt 6 for Windows (MSVC)")
set(OVERTEON_HOST_QT "$ENV{HOME}/Qt-host/6.9.3/gcc_64"       CACHE PATH "native Qt 6 (host tools)")

if(EXISTS "${OVERTEON_MSVC_QT}")
    list(APPEND CMAKE_PREFIX_PATH "${OVERTEON_MSVC_QT}")
    set(CMAKE_FIND_ROOT_PATH ${CMAKE_FIND_ROOT_PATH} "${OVERTEON_MSVC_QT}")
endif()
if(EXISTS "${OVERTEON_HOST_QT}/lib/cmake/Qt6CoreTools")
    set(QT_HOST_PATH "${OVERTEON_HOST_QT}" CACHE PATH "" FORCE)
    set(QT_HOST_PATH_CMAKE_DIR "${OVERTEON_HOST_QT}" CACHE PATH "" FORCE)
endif()

# The xwin splat has the RELEASE CRT only (msvcrt.lib / libcmt.lib), not the
# debug variants, so a Debug build cannot link.  Pin the runtime and the build
# type instead of letting CMake default to a debug configuration.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "" FORCE)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
