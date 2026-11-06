cmake_minimum_required(VERSION 3.22)
# OnyxTEON — the Qt6/QML fork of Overteon.  Separate product identity so both
# can be installed and loaded side by side in the same host:
#   * own LV2 URI / bundle id / plug-in code
#   * own bundle + product name (OnyxTEON.lv2, OnyxTEON.vst3)
#   * identical parameter IDs, so .hinj presets stay interchangeable.
project(OnyxTEON VERSION 0.9.15)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Single-config generators (Ninja/Unix Makefiles) leave CMAKE_BUILD_TYPE empty
# by default, which means no optimisation at all. Default to Release.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Let editors/tools (clangd, cppcheck, ...) see the Ninja build.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The Qt front end depends on Qt only — no JUCE.  This option builds just the
# host-free viewer, which is what makes a MinGW cross-build possible: JUCE
# refuses MinGW outright (juce_TargetPlatform.h), Qt does not.
option(OVERTEON_PREVIEW_ONLY "Build only overteon-qml-preview (no JUCE plugin)" OFF)

if(OVERTEON_PREVIEW_ONLY)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick Widgets QuickWidgets)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    add_executable(overteon-qml-preview
        ui/qt/preview/main.cpp
        ui/qt/OvEngine.cpp
        ui/qt/OvParam.cpp
        ui/qt/OvEqView.cpp
        ui/qt/OvMeterView.cpp
        ui/qt/OvSeamFrost.cpp
        ui/qt/ov_ui.qrc)
    target_include_directories(overteon-qml-preview PRIVATE ui/qt)
    target_link_libraries(overteon-qml-preview PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Widgets Qt6::QuickWidgets)

    # the preview checks for a compositing manager before Qt starts
    if(UNIX AND NOT APPLE)
        find_package(X11 REQUIRED)
        target_link_libraries(overteon-qml-preview PRIVATE X11::X11)
        foreach(_ext X11::Xext Xext)
            if(TARGET ${_ext})
                target_link_libraries(overteon-qml-preview PRIVATE ${_ext})
                break()
            endif()
        endforeach()
    endif()
    set_target_properties(overteon-qml-preview PROPERTIES AUTOMOC ON AUTORCC ON)
    if(CMAKE_CROSSCOMPILING AND DEFINED ENV{OVERTEON_HOST_RCC})
        set_target_properties(overteon-qml-preview PROPERTIES
            AUTORCC_EXECUTABLE "$ENV{OVERTEON_HOST_RCC}")
    endif()
    if(WIN32)
        # a GUI subsystem binary so wine does not open a console window
        set_target_properties(overteon-qml-preview PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    return()
endif()

include(FetchContent)

# Use the JUCE tree that is already downloaded into ./JUCE (JUCE 8.0.13, i.e.
# exactly the tag pinned below). FetchContent then does no network I/O at all,
# so configure/build works offline. Pass -DOVERTEON_VENDORED_JUCE=OFF to make
# FetchContent clone from GitHub instead (needs proxychains on this machine).
option(OVERTEON_VENDORED_JUCE
       "Use the downloaded ./JUCE tree instead of cloning from GitHub" ON)

if(OVERTEON_VENDORED_JUCE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_JUCE "${CMAKE_CURRENT_SOURCE_DIR}/JUCE"
        CACHE PATH "Vendored JUCE checkout (skips the GitHub download)" FORCE)
endif()

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.13
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE)
FetchContent_MakeAvailable(JUCE)

# LV2 is a first-class JUCE format on all three platforms (no extra SDK needed).
# Leave it out explicitly with -DOVERTEON_FORMATS="VST3;Standalone".
set(PLUGIN_FORMATS LV2 VST3 Standalone)
set(OVERTEON_FORMATS "${PLUGIN_FORMATS}" CACHE STRING "JUCE plug-in formats to build")
set(PLUGIN_FORMATS ${OVERTEON_FORMATS})
if(APPLE)
    list(APPEND PLUGIN_FORMATS AU)
    # Universal binary (Apple Silicon + Intel) unless caller overrides.
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
    # Reasonable deployment floor for current DAWs.
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()
endif()

# ============================================================
# Overteon — harmonic-injection EQ (active distortion + EQ)
# ============================================================
juce_add_binary_data(FxWebAssets
    HEADER_NAME FxData.h
    NAMESPACE FxData
    SOURCES
        web_fx/index.html
        web_fx/eonun_logo.png
        web_fx/edix.otf)

# Declared before first use: both the web-browser feature flags below and the
# FxWebEditor exclusion depend on it, and CMake reads variables in order.
option(OVERTEON_QT_UI "Build the Qt6/QML editor instead of the WebView one" ON)

# The WebView editor needs the platform web engine; the Qt editor does not, and
# requiring it would also force the WebView2 NuGet package on a cross-build.
if(OVERTEON_QT_UI)
    set(OVERTEON_NEEDS_WEB_BROWSER FALSE)
    set(OVERTEON_NEEDS_WEBVIEW2 FALSE)
else()
    set(OVERTEON_NEEDS_WEB_BROWSER TRUE)
    set(OVERTEON_NEEDS_WEBVIEW2 TRUE)
endif()

juce_add_plugin(HarmonicInjector
    COMPANY_NAME "Eonun"
    COMPANY_WEBSITE "https://eonun.com"
    # Stable LV2 URIs/ids must never change after release, or hosts lose settings.
    LV2URI "https://eonun.com/plugins/OnyxTEON"
    PLUGIN_MANUFACTURER_CODE Eonu
    BUNDLE_ID com.eonun.onyxteon
    HARDENED_RUNTIME_ENABLED TRUE
    PLUGIN_CODE Onyx
    FORMATS ${PLUGIN_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    NEEDS_WEB_BROWSER ${OVERTEON_NEEDS_WEB_BROWSER}
    NEEDS_WEBVIEW2 ${OVERTEON_NEEDS_WEBVIEW2}
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME "OnyxTEON")

target_sources(HarmonicInjector PRIVATE
    src/FxProcessor.cpp)

# FxWebEditor implements the JUCE WebBrowserComponent editor.  With the Qt
# editor enabled JUCE_WEB_BROWSER is 0, so juce::WebBrowserComponent does not
# exist and this file cannot compile.  Keep it in the source list (so IDE and
# WebView builds still see it) but tell CMake not to compile it.
if(OVERTEON_QT_UI)
    set_source_files_properties(src/FxWebEditor.cpp PROPERTIES
        HEADER_FILE_ONLY TRUE)
endif()

# ---------------------------------------------------------------------------
#  Qt6 / QML editor
#
#  The DSP, parameters and host integration stay JUCE; only the editor is Qt.
#  OVERTEON_QT_UI=OFF falls back to the WebView editor.
# ---------------------------------------------------------------------------
if(OVERTEON_QT_UI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick Widgets QuickWidgets)

    # HarmonicInjector was created by juce_add_plugin before this point, so
    # AUTOMOC has to be enabled on the target itself (not via CMAKE_AUTOMOC).
    set_target_properties(HarmonicInjector PROPERTIES AUTOMOC ON AUTORCC ON)

    target_sources(HarmonicInjector PRIVATE
        ui/qt/OvEngine.cpp
        ui/qt/OvParam.cpp
        ui/qt/OvJuceSource.cpp
        ui/qt/OvEqView.cpp
        ui/qt/OvMeterView.cpp
        ui/qt/OvSeamFrost.cpp
        ui/qt/OvQmlEditor.cpp)

    target_include_directories(HarmonicInjector PRIVATE ui/qt src)

    target_compile_definitions(HarmonicInjector PUBLIC OVERTEON_QT_UI=1)

    target_link_libraries(HarmonicInjector PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)

    # OvQmlEditor walks and maps the host's X window chain itself
    if(UNIX AND NOT APPLE)
        find_package(X11 REQUIRED)
        target_link_libraries(HarmonicInjector PRIVATE X11::X11)
        # XShapeCombineRectangles (input shape, used by the optional
        # ONYXTEON_CLEAR_INPUT path) lives in libXext
        foreach(_x11_ext X11::Xext Xext)
            if(TARGET ${_x11_ext})
                target_link_libraries(HarmonicInjector PRIVATE ${_x11_ext})
                break()
            endif()
        endforeach()
    endif()

    # The QML + font bundle has to be compiled into every plugin binary: the
    # shared-code target is a static archive and nothing references the rcc
    # object, so a plain target_sources there would be dropped by the linker.
    foreach(_fmt ${PLUGIN_FORMATS})
        if(TARGET HarmonicInjector_${_fmt})
            target_sources(HarmonicInjector_${_fmt} PRIVATE ui/qt/ov_ui.qrc)
            set_target_properties(HarmonicInjector_${_fmt} PROPERTIES AUTORCC ON)
        endif()
    endforeach()

    # host-free viewer: same QML, mock parameter source
    add_executable(overteon-qml-preview
        ui/qt/preview/main.cpp
        ui/qt/OvEngine.cpp
        ui/qt/OvParam.cpp
        ui/qt/OvEqView.cpp
        ui/qt/OvMeterView.cpp
        ui/qt/OvSeamFrost.cpp
        ui/qt/ov_ui.qrc)

    target_include_directories(overteon-qml-preview PRIVATE ui/qt)
    target_link_libraries(overteon-qml-preview PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Widgets Qt6::QuickWidgets)

    # the preview checks for a compositing manager before Qt starts
    if(UNIX AND NOT APPLE)
        find_package(X11 REQUIRED)
        target_link_libraries(overteon-qml-preview PRIVATE X11::X11)
        foreach(_ext X11::Xext Xext)
            if(TARGET ${_ext})
                target_link_libraries(overteon-qml-preview PRIVATE ${_ext})
                break()
            endif()
        endforeach()
    endif()
    set_target_properties(overteon-qml-preview PROPERTIES
        AUTOMOC ON AUTORCC ON)
    if(NOT WIN32)
        target_compile_definitions(overteon-qml-preview PRIVATE BUILD_UNIXLIKE=1)
    endif()

    # headless check that the dual-Gaussian really blurs
    add_executable(onyx-blur-test
        tools/units/blur_test.cpp
        ui/qt/OvEngine.cpp
        ui/qt/OvParam.cpp
        ui/qt/OvEqView.cpp
        ui/qt/OvMeterView.cpp
        ui/qt/OvSeamFrost.cpp)
    target_include_directories(onyx-blur-test PRIVATE ui/qt)
    target_link_libraries(onyx-blur-test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
    set_target_properties(onyx-blur-test PROPERTIES AUTOMOC ON)
endif()

# Platform switch used by the sources: Windows vs. everything unix-like
# (Linux, macOS, BSD). Mirrors JUCE_WINDOWS and costs nothing.
if(NOT WIN32)
    set(OVERTEON_UNIXLIKE TRUE)
endif()

# JUCE_WEB_BROWSER=1 pulls juce::WebBrowserComponent in, which on Linux means
# gtk/webkit2gtk headers.  The Qt editor does not need any of that, so the flag
# has to follow OVERTEON_QT_UI -- otherwise a Qt build still demands the
# webkit2gtk-4.1 dev package.
if(OVERTEON_QT_UI)
    set(OVERTEON_WEB_BROWSER 0)
else()
    set(OVERTEON_WEB_BROWSER 1)
endif()

target_compile_definitions(HarmonicInjector PUBLIC
    JUCE_WEB_BROWSER=${OVERTEON_WEB_BROWSER}
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    $<$<NOT:$<PLATFORM_ID:Windows>>:BUILD_UNIXLIKE=1>
    $<$<AND:$<PLATFORM_ID:Windows>,$<NOT:$<BOOL:${OVERTEON_QT_UI}>>>:JUCE_USE_WIN_WEBVIEW2=1>
    $<$<AND:$<PLATFORM_ID:Windows>,$<NOT:$<BOOL:${OVERTEON_QT_UI}>>>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>)

target_link_libraries(HarmonicInjector
    PRIVATE
        FxWebAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
