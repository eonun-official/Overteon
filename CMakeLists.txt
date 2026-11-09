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
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick Widgets QuickWidgets ShaderTools)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    add_executable(overteon-qml-preview
        ui/qt/preview/main.cpp
        ui/qt/OvEngine.cpp
        ui/qt/OvParam.cpp
        ui/qt/OvEqView.cpp
        ui/qt/OvFogSource.cpp ui/qt/OvFrameProbe.cpp
        ui/qt/OvMeterView.cpp
        ui/qt/OvSeamFrost.cpp ui/qt/OvBlurTest.cpp
        ui/qt/OvFrost.cpp
        ui/qt/ov_ui.qrc)
    target_include_directories(overteon-qml-preview PRIVATE ui/qt)
    target_link_libraries(overteon-qml-preview PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Widgets Qt6::QuickWidgets
        # OvEqView is a QQuickFramebufferObject and paints with a
        # QOpenGLPaintDevice: GL tessellates the paths instead of the CPU
        # rasterising them into an image.
        Qt6::OpenGL)

    # the preview checks for a compositing manager before Qt starts
    if(UNIX AND NOT APPLE)
        find_package(X11 REQUIRED)
        target_link_libraries(overteon-qml-preview PRIVATE X11::X11)
        # --jack: the preview as a real JACK client, so the metering can be
        # driven by actual audio without a plug-in host in the way.
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(JACK REQUIRED jack)
        target_include_directories(overteon-qml-preview PRIVATE ${JACK_INCLUDE_DIRS})
        target_link_libraries(overteon-qml-preview PRIVATE ${JACK_LIBRARIES})
        target_link_directories(overteon-qml-preview PRIVATE ${JACK_LIBRARY_DIRS})
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

# VST2 needs an SDK on the include path.  Steinberg stopped shipping one in 2018,
# so third_party/vst2sdk/ carries the two headers JUCE's wrapper includes, written
# against the clean-room reimplementation vendored beside them (see the notes at
# the top of aeffect.h).  Set up before the plug-in targets are created — JUCE
# refuses to add a VST target otherwise.
juce_set_vst2_sdk_path("${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst2sdk")

# LV2 is a first-class JUCE format on all three platforms (no extra SDK needed).
# Leave it out explicitly with -DOVERTEON_FORMATS="VST3;Standalone".
set(PLUGIN_FORMATS LV2 VST3 VST Standalone)
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
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick Widgets QuickWidgets ShaderTools)

    # HarmonicInjector was created by juce_add_plugin before this point, so
    # AUTOMOC has to be enabled on the target itself (not via CMAKE_AUTOMOC).
    set_target_properties(HarmonicInjector PROPERTIES AUTOMOC ON AUTORCC ON)

    target_sources(HarmonicInjector PRIVATE
        ui/qt/OvEngine.cpp
        ui/qt/OvParam.cpp
        ui/qt/OvJuceSource.cpp
        ui/qt/OvEqView.cpp
        ui/qt/OvFogSource.cpp ui/qt/OvFrameProbe.cpp
        ui/qt/OvMeterView.cpp
        ui/qt/OvSeamFrost.cpp ui/qt/OvBlurTest.cpp
        ui/qt/OvFrost.cpp
        ui/qt/OvQmlEditor.cpp
        ui/qt/OvQtComponent.cpp)

    target_include_directories(HarmonicInjector PRIVATE ui/qt src)

    target_compile_definitions(HarmonicInjector PUBLIC OVERTEON_QT_UI=1)

    # QQuickRenderControl renders the scene into a texture we own instead of
    # onto a window of Qt's own.  That is what lets the editor live inside a
    # JUCE Component on every platform: no reparenting, so no window the host
    # can unmap behind our back.
    target_link_libraries(HarmonicInjector PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::OpenGL)

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

    # ---- GPU frost shaders ------------------------------------------------
    # Qt 6 cannot compile GLSL at run time, so the sources are baked to .qsb
    # and shipped in their own resource bundle (rcc needs literal paths, so a
    # ${VAR} entry in ov_ui.qrc would not work).
    # The dual-Gaussian kernel lives in ONE file (blurfunc.glsl) and is spliced
    # into the fragment shaders that call it.  qsb has no #include, so the
    # splice happens here: the function is inserted after the #version line.
    # One definition means the glass and the EQ fog cannot drift apart.
    set(_ov_blurfunc "${CMAKE_CURRENT_SOURCE_DIR}/ui/qt/shaders/blurfunc.glsl")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_ov_blurfunc})
    file(READ ${_ov_blurfunc} _ov_blurfunc_src)

    set(OVERTEON_SHADER_QSBS "")
    foreach(_sh
        ui/qt/shaders/blur.vert  ui/qt/shaders/blur.frag
        ui/qt/shaders/glass.vert ui/qt/shaders/glass.frag)
        get_filename_component(_shname ${_sh} NAME)
        set(_qsb "${CMAKE_CURRENT_BINARY_DIR}/ov_shaders/${_shname}.qsb")
        set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${_sh}")

        if(_shname STREQUAL "blur.frag")
            # strip the shader's own #version line, prepend the function behind
            # a single #version — GLSL requires it to be the first statement.
            file(READ ${_src} _body)
            string(REGEX REPLACE "^#version[^\n]*\n" "" _body "${_body}")
            set(_src "${CMAKE_CURRENT_BINARY_DIR}/ov_shaders/${_shname}.glsl")
            file(WRITE ${_src} "#version 440\n${_ov_blurfunc_src}\n${_body}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                         ${CMAKE_CURRENT_SOURCE_DIR}/${_sh})
        endif()

        add_custom_command(OUTPUT ${_qsb}
            COMMAND /usr/lib/qt6/bin/qsb --glsl 100,120,150,300es,310es,320,330,410,440
                    -o ${_qsb} ${_src}
            DEPENDS ${_src} ${_ov_blurfunc}
            COMMENT "qsb ${_shname}")
        list(APPEND OVERTEON_SHADER_QSBS ${_qsb})
    endforeach()
    add_custom_target(overteon_shaders DEPENDS ${OVERTEON_SHADER_QSBS})

    # ---- ovctl: drive a running preview through shared memory -------------
    # Small enough to be a plain executable with no Qt dependency, so it can be
    # run from a script while the UI is busy.
    add_executable(ovctl tools/ovctl.cpp)
    target_include_directories(ovctl PRIVATE ui/qt)
    target_link_libraries(ovctl PRIVATE ${CMAKE_THREAD_LIBS_INIT})

    set(OVERTEON_SHADER_QRC "${CMAKE_CURRENT_BINARY_DIR}/ov_shaders/ov_shaders.qrc")
    file(WRITE ${OVERTEON_SHADER_QRC}
        "<RCC><qresource prefix=\"/shaders\">\n"
        "  <file alias=\"blur.vert.qsb\">blur.vert.qsb</file>\n"
        "  <file alias=\"blur.frag.qsb\">blur.frag.qsb</file>\n"
        "  <file alias=\"glass.vert.qsb\">glass.vert.qsb</file>\n"
        "  <file alias=\"glass.frag.qsb\">glass.frag.qsb</file>\n"
        "</qresource></RCC>\n")

    # The QML + font bundle has to be compiled into every plugin binary: the
    # shared-code target is a static archive and nothing references the rcc
    # object, so a plain target_sources there would be dropped by the linker.
    # The GPU frost blurs live in their own resource bundle, and a plug-in that
    # is missing it does not fail loudly: every ShaderEffect silently renders
    # nothing, so the seam glass and the EQ fog just vanish in the host while
    # the preview (which does link the bundle) looks right.  Only the host's log
    # says so:
    #
    #   Failed to find shader ":/shaders/glass.vert.qsb"
    #   ShaderEffect: shader preparation failed for qrc:/shaders/glass.frag.qsb
    #
    # So the bundle goes into every plug-in binary, exactly like the QML one,
    # and the shaders are built first because AUTORCC must find the .qsb files.
    foreach(_fmt ${PLUGIN_FORMATS})
        if(TARGET HarmonicInjector_${_fmt})
            target_sources(HarmonicInjector_${_fmt} PRIVATE
                ui/qt/ov_ui.qrc ${OVERTEON_SHADER_QRC})
            set_target_properties(HarmonicInjector_${_fmt} PROPERTIES AUTORCC ON)
            add_dependencies(HarmonicInjector_${_fmt} overteon_shaders)

            # -z,nodelete: the host must not unmap this module while it runs.
            #
            # Qt is used as a GUEST here: one QGuiApplication per process, made
            # on first editor use and deliberately leaked, because the host's
            # main loop keeps running between editors.  suil, however, dlcloses
            # the plug-in as soon as the editor is destroyed, and the objects
            # and metaobject strings that application still points at go with
            # it.  The next queued event is then delivered through a vtable in
            # an unmapped page:
            #
            #   #0 QCoreApplicationPrivate::notify_helper(QObject*, QEvent*)
            #   #2 QCoreApplicationPrivate::sendPostedEvents()
            #   ... g_main_context_iteration() ... g_application_run()
            #
            # i.e. jalv.gtk3 SIGSEGV (exit 139) every time its window is closed.
            # Keeping the module resident costs nothing that matters -- it is
            # the same 8 MB the editor already uses -- and closing the window
            # then exits with status 0, measured over repeated runs.
            target_link_options(HarmonicInjector_${_fmt} PRIVATE "LINKER:-z,nodelete")
        endif()
    endforeach()

    # host-free viewer: same QML, mock parameter source
    add_executable(overteon-qml-preview
        ui/qt/preview/main.cpp
        ui/qt/OvEngine.cpp
        ui/qt/OvParam.cpp
        ui/qt/OvEqView.cpp
        ui/qt/OvFogSource.cpp ui/qt/OvFrameProbe.cpp
        ui/qt/OvMeterView.cpp
        ui/qt/OvSeamFrost.cpp ui/qt/OvBlurTest.cpp
        ui/qt/OvFrost.cpp
        ui/qt/ov_ui.qrc
        ${OVERTEON_SHADER_QRC})

    target_include_directories(overteon-qml-preview PRIVATE ui/qt src)

    # ---- the REAL DSP ------------------------------------------------
    # The preview used to FFT the raw input and then ADD an analytic EQ curve on
    # top of it to produce the "after" picture.  That is not an after-processing
    # meter: with no signal the input sits on its -85 dB floor, and adding the
    # curve to that floor drew the EQ shape as a static image — the "in silence
    # it shows a constant value" report.  Because it was a model it could never
    # show what the DSP actually does either (oversampling, saturation
    # harmonics, dry/wet, the limiter).
    #
    # The preview therefore hosts the plug-in's own InjectorProcessor: the JACK
    # thread copies incoming frames into a JUCE buffer and calls processBlock(),
    # exactly as a host would, and both spectrum taps are read from inside the
    # DSP.  Parameters, presets and meters come from the same APVTS the plug-in
    # uses, so ovctl and .hinj files drive the real thing.
    target_sources(overteon-qml-preview PRIVATE
        src/FxProcessor.cpp
        ui/qt/OvJuceSource.cpp
        # createEditor() is part of the processor; the preview never opens the
        # editor, but the symbol has to link.
        ui/qt/OvQmlEditor.cpp ui/qt/OvQtComponent.cpp)
    target_compile_definitions(overteon-qml-preview PRIVATE
        OVERTEON_PREVIEW_HAS_DSP=1 OVERTEON_QT_UI=1
        # Same JUCE feature set as the plug-in target.  Without these the module
        # sources are compiled with the browser enabled and juce_gui_extra pulls
        # in gtk, which is neither needed nor installed here.
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        $<$<NOT:$<PLATFORM_ID:Windows>>:BUILD_UNIXLIKE=1>)
    target_link_libraries(overteon-qml-preview PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
    target_link_libraries(overteon-qml-preview PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Widgets Qt6::QuickWidgets
        # OvEqView is a QQuickFramebufferObject and paints with a
        # QOpenGLPaintDevice: GL tessellates the paths instead of the CPU
        # rasterising them into an image.
        Qt6::OpenGL)

    # the preview checks for a compositing manager before Qt starts
    if(UNIX AND NOT APPLE)
        find_package(X11 REQUIRED)
        target_link_libraries(overteon-qml-preview PRIVATE X11::X11)
        # --jack: the preview as a real JACK client, so the metering can be
        # driven by actual audio without a plug-in host in the way.
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(JACK REQUIRED jack)
        target_include_directories(overteon-qml-preview PRIVATE ${JACK_INCLUDE_DIRS})
        target_link_libraries(overteon-qml-preview PRIVATE ${JACK_LIBRARIES})
        target_link_directories(overteon-qml-preview PRIVATE ${JACK_LIBRARY_DIRS})
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
        ui/qt/OvFogSource.cpp ui/qt/OvFrameProbe.cpp
        ui/qt/OvMeterView.cpp
        ui/qt/OvSeamFrost.cpp ui/qt/OvBlurTest.cpp
        ui/qt/OvFrost.cpp)
    target_include_directories(onyx-blur-test PRIVATE ui/qt)
    target_link_libraries(onyx-blur-test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
    set_target_properties(onyx-blur-test PROPERTIES AUTOMOC ON)

    # headless proof of the meter interpolation: smootherstep's C2 ends,
    # Catmull-Rom not overshooting, and a fall that takes the same time at
    # 15 Hz as at 120 Hz.  No Qt, no audio, no window -- it is arithmetic.
    add_executable(onyx-ballistics-test tools/units/ballistics_test.cpp)
    target_include_directories(onyx-ballistics-test PRIVATE ui/qt)

    # headless proof that the analyser is smooth: a steady signal's outline must
    # not move more than ~1 dB a frame, and the old 1/6-octave/dB-domain chain is
    # built beside it for comparison.  Same conditions, same source, so the two
    # numbers are a measurement rather than a memory.
    add_executable(onyx-spectrum-test tools/units/spectrum_test.cpp)
    target_include_directories(onyx-spectrum-test PRIVATE ui/qt)
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
