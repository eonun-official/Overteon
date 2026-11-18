cmake_minimum_required(VERSION 3.22)
# Overteon — the Qt6/QML fork of Overteon.  Separate product identity so both
# can be installed and loaded side by side in the same host:
#   * own LV2 URI / bundle id / plug-in code
#   * own bundle + product name (Overteon.lv2, Overteon.vst3)
#   * identical parameter IDs, so .hinj presets stay interchangeable.
project(Overteon VERSION 0.9.20)

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

# ---- can the host resize the editor window? -------------------------------
#
# OFF by default.  The UI is drawn against one design size (1040x772) and scales
# uniformly to fit whatever frame it is given, so it *can* live in any size — but
# the design is the product, and a window a host has dragged down to 400x300 is
# unreadable rather than flexible.
#
# With OFF the editor asks the host for its design size and tells it the window
# is not resizable, so the size it asks for is the size it keeps.  With ON the
# host may stretch it and the design follows the frame.
#
# This is a build decision, not a runtime switch: a fixed build cannot be
# widened by anything, which is the point of it being a build flag.
option(OVERTEON_RESIZABLE_WINDOW "Let the host resize the plug-in editor" ON)

if(OVERTEON_PREVIEW_ONLY)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick Widgets QuickWidgets ShaderTools)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    add_executable(overteon-qml-preview
        ../Source/ui/qt/preview/main.cpp
        ../Source/ui/qt/OvEngine.cpp
        ../Source/ui/qt/OvPointer.cpp
        ../Source/ui/qt/OvParam.cpp
        ../Source/ui/qt/OvEqView.cpp
        ../Source/ui/qt/OvFogSource.cpp ../Source/ui/qt/OvFrameProbe.cpp
        ../Source/ui/qt/OvMeterView.cpp
        ../Source/ui/qt/OvSeamFrost.cpp ../Source/ui/qt/OvBlurTest.cpp
        ../Source/ui/qt/OvFrost.cpp
        ../Source/ui/qt/ov_ui.qrc)
    target_include_directories(overteon-qml-preview PRIVATE ../Source/ui/qt)
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

# Use the JUCE tree that is already downloaded into Source/third_party/JUCE
# (JUCE 8.0.13, i.e. exactly the tag pinned below). FetchContent then does no
# network I/O at all, so configure/build works offline. Pass
# -DOVERTEON_VENDORED_JUCE=OFF to make FetchContent clone from GitHub instead
# (needs proxychains on this machine).
option(OVERTEON_VENDORED_JUCE
       "Use the vendored JUCE tree instead of cloning from GitHub" ON)

if(OVERTEON_VENDORED_JUCE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Source/third_party/JUCE/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_JUCE "${CMAKE_CURRENT_SOURCE_DIR}/../Source/third_party/JUCE"
        CACHE PATH "Vendored JUCE checkout (skips the GitHub download)" FORCE)
endif()

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.13
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE)
FetchContent_MakeAvailable(JUCE)

# VST2 needs an SDK on the include path.  Steinberg stopped shipping one in 2018,
# so ../Source/third_party/vst2sdk/ carries the two headers JUCE's wrapper includes, written
# against the clean-room reimplementation vendored beside them (see the notes at
# the top of aeffect.h).  Set up before the plug-in targets are created — JUCE
# refuses to add a VST target otherwise.
juce_set_vst2_sdk_path("${CMAKE_CURRENT_SOURCE_DIR}/../Source/third_party/vst2sdk")

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
juce_add_binary_data(OverteonWebAssets
    HEADER_NAME FxData.h
    NAMESPACE FxData
    SOURCES
        ../Source/web_fx/index.html
        ../Source/web_fx/eonun_logo.png
        ../Source/web_fx/edix.otf)

# Declared before first use: both the web-browser feature flags below and the
# OverteonWebEditor exclusion depend on it, and CMake reads variables in order.
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

juce_add_plugin(Overteon
    COMPANY_NAME "Eonun"
    COMPANY_WEBSITE "https://eonun.com"
    # Stable LV2 URIs/ids must never change after release, or hosts lose settings.
    LV2URI "https://eonun.com/plugins/Overteon"
    PLUGIN_MANUFACTURER_CODE Eonu
    BUNDLE_ID com.eonun.overteon
    HARDENED_RUNTIME_ENABLED TRUE
    PLUGIN_CODE Ovtn
    FORMATS ${PLUGIN_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    NEEDS_WEB_BROWSER ${OVERTEON_NEEDS_WEB_BROWSER}
    NEEDS_WEBVIEW2 ${OVERTEON_NEEDS_WEBVIEW2}
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME "Overteon")

target_sources(Overteon PRIVATE
    ../Source/src/OverteonProcessor.cpp)

# OverteonWebEditor implements the JUCE WebBrowserComponent editor.  With the Qt
# editor enabled JUCE_WEB_BROWSER is 0, so juce::WebBrowserComponent does not
# exist and this file cannot compile.  Keep it in the source list (so IDE and
# WebView builds still see it) but tell CMake not to compile it.
if(OVERTEON_QT_UI)
    set_source_files_properties(../Source/src/OverteonWebEditor.cpp PROPERTIES
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

    # Overteon was created by juce_add_plugin before this point, so
    # AUTOMOC has to be enabled on the target itself (not via CMAKE_AUTOMOC).
    set_target_properties(Overteon PROPERTIES AUTOMOC ON AUTORCC ON)

    target_sources(Overteon PRIVATE
        ../Source/ui/qt/OvEngine.cpp
        ../Source/ui/qt/OvPointer.cpp
        ../Source/ui/qt/OvParam.cpp
        ../Source/ui/qt/OvJuceSource.cpp
        ../Source/ui/qt/OvEqView.cpp
        ../Source/ui/qt/OvFogSource.cpp ../Source/ui/qt/OvFrameProbe.cpp
        ../Source/ui/qt/OvMeterView.cpp
        ../Source/ui/qt/OvSeamFrost.cpp ../Source/ui/qt/OvBlurTest.cpp
        ../Source/ui/qt/OvFrost.cpp
        ../Source/ui/qt/OvQmlEditor.cpp
        ../Source/ui/qt/OvQtComponent.cpp

        # Repairs libQt6Core's OpenSSL imports when the host has let another
        # plug-in's statically-linked OpenSSL win the process symbol scope.
        # Ardour/Mixbus ship Harrison LV2 plug-ins that do exactly that, and
        # without this the first QML compile jumps to a garbage address.  See
        # the file for the whole chain.
        ../Source/ui/qt/OvCryptoShim.cpp)

    if(UNIX AND NOT APPLE)
        # Fills in the glib symbols a host's bundled-but-outdated glib lacks.
        # Qt6Qml NEEDs Qt6Network, which NEEDs libproxy -> libpxbackend, and
        # libpxbackend is built against glib >= 2.80.  Ardour's official binary
        # ships its own older glib first on LD_LIBRARY_PATH, so loading this
        # plug-in failed with "libpxbackend-1.0.so: undefined symbol:
        # g_strv_builder_unref" before any of our code ran.  See the file.
        target_sources(Overteon PRIVATE ../Source/ui/qt/OvGlibShim.cpp)

        # Those symbols are the WHOLE POINT of that file, so they have to survive
        # to the dynamic symbol table.  Two things would otherwise remove them:
        #
        #   * the target is built with -fvisibility=hidden, so they have to be
        #     compiled with default visibility (the attribute in the source is not
        #     enough on its own for every toolchain);
        #   * the plug-in links with -flto, and link-time optimisation internalises a
        #     global that nothing in the program references -- which is exactly the
        #     case here, since the reference lives in libpxbackend.  Without
        #     -fno-lto the object is optimised away and the .so exports nothing.
        #     Verified: nm on the .so reported 0 of them until LTO was off.
        set_source_files_properties(../Source/ui/qt/OvGlibShim.cpp PROPERTIES
            COMPILE_OPTIONS "-fvisibility=default;-fno-lto"
            COMPILE_FLAGS   "-fvisibility=default -fno-lto")
    endif()

    # (The link-side half of this -- forcing OvGlibShim.o out of the shared
    # archive -- is per-format and lives in the PLUGIN_FORMATS loop below,
    # because options set on Overteon do not reach the .so.)

    target_include_directories(Overteon PRIVATE ../Source/ui/qt ../Source/src)

    target_compile_definitions(Overteon PUBLIC OVERTEON_QT_UI=1)

    # One source of truth for the version string: the About panel shows it, so
    # it comes from project(VERSION) rather than being typed into the QML.
    target_compile_definitions(Overteon PUBLIC
        OVERTEON_VERSION="${PROJECT_VERSION}")

    # Editor resizability is compiled in or out, so a fixed build cannot be
    # widened at run time.  PUBLIC: every format target (LV2/VST3/VST/
    # Standalone) compiles OvQmlEditor.cpp and has to agree.
    target_compile_definitions(Overteon PUBLIC
        OVERTEON_RESIZABLE_WINDOW=$<BOOL:${OVERTEON_RESIZABLE_WINDOW}>)

    # QQuickRenderControl renders the scene into a texture we own instead of
    # onto a window of Qt's own.  That is what lets the editor live inside a
    # JUCE Component on every platform: no reparenting, so no window the host
    # can unmap behind our back.
    target_link_libraries(Overteon PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::OpenGL)

    if(WIN32 AND NOT QT6_IS_SHARED_LIBS_BUILD)
        # A MODULE library does not get Qt's executable finalizers, so import
        # the platform and QML plugins explicitly. Qt was built with /MT; every
        # Overteon target must use the same runtime to remain a single binary.
        set_property(TARGET Overteon PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_link_libraries(Overteon PRIVATE
            Qt6::QWindowsIntegrationPlugin
            Qt6::qtquick2plugin
            Qt6::qmlplugin
            Qt6::modelsplugin
            Qt6::workerscriptplugin)
    endif()

    # OvQmlEditor walks and maps the host's X window chain itself
    if(UNIX AND NOT APPLE)
        find_package(X11 REQUIRED)
        target_link_libraries(Overteon PRIVATE X11::X11)
        # XShapeCombineRectangles (input shape, used by the optional
        # OVERTEON_CLEAR_INPUT path) lives in libXext
        foreach(_x11_ext X11::Xext Xext)
            if(TARGET ${_x11_ext})
                target_link_libraries(Overteon PRIVATE ${_x11_ext})
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
    set(_ov_blurfunc "${CMAKE_CURRENT_SOURCE_DIR}/../Source/ui/qt/shaders/blurfunc.glsl")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_ov_blurfunc})
    file(READ ${_ov_blurfunc} _ov_blurfunc_src)

    set(OVERTEON_SHADER_QSBS "")
    foreach(_sh
        ../Source/ui/qt/shaders/blur.vert  ../Source/ui/qt/shaders/blur.frag
        ../Source/ui/qt/shaders/glass.vert ../Source/ui/qt/shaders/glass.frag)
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
            COMMAND Qt6::qsb --glsl 100,120,150,300es,310es,320,330,410,440
                    -o ${_qsb} ${_src}
            DEPENDS ${_src} ${_ov_blurfunc}
            COMMENT "qsb ${_shname}")
        list(APPEND OVERTEON_SHADER_QSBS ${_qsb})
    endforeach()
    add_custom_target(overteon_shaders DEPENDS ${OVERTEON_SHADER_QSBS})

    # ---- ovctl: drive a running preview through shared memory -------------
    # Small enough to be a plain executable with no Qt dependency, so it can be
    # run from a script while the UI is busy.
    #
    # POSIX only: OvCtl.h is a POSIX shared-memory block (shm_open/mmap) and the
    # header has no Windows path.  It is a development tool for driving the
    # preview, so a Windows build simply does not produce one.
    if(NOT WIN32)
        add_executable(ovctl ../Tools/ovctl.cpp)
        target_include_directories(ovctl PRIVATE ../Source/ui/qt)
        target_link_libraries(ovctl PRIVATE ${CMAKE_THREAD_LIBS_INIT})
    endif()

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
        if(TARGET Overteon_${_fmt})
            target_sources(Overteon_${_fmt} PRIVATE
                ../Source/ui/qt/ov_ui.qrc ${OVERTEON_SHADER_QRC})
            set_target_properties(Overteon_${_fmt} PROPERTIES AUTORCC ON)
            add_dependencies(Overteon_${_fmt} overteon_shaders)

            if(WIN32 AND NOT QT6_IS_SHARED_LIBS_BUILD)
                set_property(TARGET Overteon_${_fmt} PROPERTY MSVC_RUNTIME_LIBRARY
                    "MultiThreaded$<$<CONFIG:Debug>:Debug>")
            endif()

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
            # ELF only.  Windows has no equivalent, and lld-link reads "-z"
            # as a file name ("could not open 'nodelete'").
            if(UNIX AND NOT APPLE)
                target_link_options(Overteon_${_fmt} PRIVATE "LINKER:-z,nodelete")
            endif()

            # Pull OvGlibShim.o out of the shared archive.
            #
            # Everything under the Overteon target lands in
            # libOverteon_SharedCode.a, and a static library only hands over a
            # member for a symbol somebody references.  Nothing in our own code
            # calls these -- the reference lives in libpxbackend, in the host's
            # process -- so without an explicit request the linker never pulls
            # the object in and the .so exports none of them.  `-u` marks each
            # name undefined, which IS that request.  Measured: 0 exported
            # before this, 14 after.
            #
            # It has to be attached to the per-format target, not to
            # Overteon: that one is the shared-code archive, and options
            # set there do not reach the .so that actually gets dlopen()ed.
            # Linux only: the glib shim exists for a host that ships an old
            # glib (Ardour's bundled build), and neither the problem nor the
            # `-u` linker flag exists on Windows.
            if(UNIX AND NOT APPLE)
                foreach(_shim_sym
                        g_once_init_enter_pointer g_once_init_leave_pointer
                        g_string_replace g_memdup2
                        g_strv_builder_new g_strv_builder_add g_strv_builder_end
                        g_strv_builder_unref
                        g_uri_parse g_uri_to_string g_uri_ref g_uri_unref
                        g_uri_get_scheme g_uri_get_host g_uri_get_port)
                    target_link_options(Overteon_${_fmt} PRIVATE
                        "LINKER:-u,${_shim_sym}")
                endforeach()
            endif()
        endif()
    endforeach()

    if(WIN32 AND QT6_IS_SHARED_LIBS_BUILD AND TARGET Overteon_VST3)
        # MSVC cannot delay-load Qt DLLs because they export data symbols. Keep
        # the host-facing VST3 free of Qt imports instead: this loader resolves
        # the real module by absolute path, which also searches its own folder
        # for Qt and the other private runtime DLLs.
        set_target_properties(Overteon_VST3 PROPERTIES
            OUTPUT_NAME "OverteonCore"
            SUFFIX ".dll")

        add_library(Overteon_VST3_Bootstrap MODULE ../Source/src/OvVst3Bootstrap.cpp)
        add_dependencies(Overteon_VST3_Bootstrap Overteon_VST3)
        # The bootstrap is what the host loads directly, before we control the
        # DLL search path. Static CRT keeps its imports down to KERNEL32, so it
        # loads even on a machine without the VC++ redistributable.
        set_property(TARGET Overteon_VST3_Bootstrap PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set_target_properties(Overteon_VST3_Bootstrap PROPERTIES
            PREFIX ""
            OUTPUT_NAME "Overteon"
            SUFFIX ".vst3"
            LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Overteon_VST3>")
    endif()

    # host-free viewer: same QML, mock parameter source
    #
    # Not built for Windows.  It is a development instrument -- it opens its own
    # window, reads parameters from a POSIX shared-memory block (ovctl) and, with
    # --jack, is a JACK client -- none of which exists on Windows.  The plug-in
    # is what ships; the viewer stays a Linux tool.
    if(NOT WIN32)
    add_executable(overteon-qml-preview
        ../Source/ui/qt/preview/main.cpp
        ../Source/ui/qt/OvEngine.cpp
        ../Source/ui/qt/OvPointer.cpp
        ../Source/ui/qt/OvParam.cpp
        ../Source/ui/qt/OvEqView.cpp
        ../Source/ui/qt/OvFogSource.cpp ../Source/ui/qt/OvFrameProbe.cpp
        ../Source/ui/qt/OvMeterView.cpp
        ../Source/ui/qt/OvSeamFrost.cpp ../Source/ui/qt/OvBlurTest.cpp
        ../Source/ui/qt/OvFrost.cpp
        ../Source/ui/qt/OvCryptoShim.cpp
        ../Source/ui/qt/ov_ui.qrc
        ${OVERTEON_SHADER_QRC})

    target_include_directories(overteon-qml-preview PRIVATE ../Source/ui/qt ../Source/src)

    # ---- the REAL DSP ------------------------------------------------
    # The preview used to FFT the raw input and then ADD an analytic EQ curve on
    # top of it to produce the "after" picture.  That is not an after-processing
    # meter: with no signal the input sits on its -85 dB floor, and adding the
    # curve to that floor drew the EQ shape as a static image — the "in silence
    # it shows a constant value" report.  Because it was a model it could never
    # show what the DSP actually does either (oversampling, saturation
    # harmonics, dry/wet, the limiter).
    #
    # The preview therefore hosts the plug-in's own OverteonProcessor: the JACK
    # thread copies incoming frames into a JUCE buffer and calls processBlock(),
    # exactly as a host would, and both spectrum taps are read from inside the
    # DSP.  Parameters, presets and meters come from the same APVTS the plug-in
    # uses, so ovctl and .hinj files drive the real thing.
    target_sources(overteon-qml-preview PRIVATE
        ../Source/src/OverteonProcessor.cpp
        ../Source/ui/qt/OvJuceSource.cpp
        # createEditor() is part of the processor; the preview never opens the
        # editor, but the symbol has to link.
        ../Source/ui/qt/OvQmlEditor.cpp ../Source/ui/qt/OvQtComponent.cpp)
    target_compile_definitions(overteon-qml-preview PRIVATE
        OVERTEON_PREVIEW_HAS_DSP=1 OVERTEON_QT_UI=1
        OVERTEON_VERSION="${PROJECT_VERSION}"
        OVERTEON_RESIZABLE_WINDOW=$<BOOL:${OVERTEON_RESIZABLE_WINDOW}>
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
    target_compile_definitions(overteon-qml-preview PRIVATE BUILD_UNIXLIKE=1)
    endif()   # NOT WIN32 -- overteon-qml-preview

    # The three headless checks below are developer tools and POSIX programs:
    # blur_test.cpp and the sources it links pull in OvEnv.h, which includes
    # <strings.h>, and the tests print to a terminal and are run by hand.  A
    # Windows build has no use for them and they do not compile there.
    if(NOT WIN32)
    # headless check that the dual-Gaussian really blurs
    add_executable(over-blur-test
        ../Tools/units/blur_test.cpp
        ../Source/ui/qt/OvEngine.cpp
        ../Source/ui/qt/OvParam.cpp
        ../Source/ui/qt/OvEqView.cpp
        ../Source/ui/qt/OvFogSource.cpp ../Source/ui/qt/OvFrameProbe.cpp
        ../Source/ui/qt/OvMeterView.cpp
        ../Source/ui/qt/OvSeamFrost.cpp ../Source/ui/qt/OvBlurTest.cpp
        ../Source/ui/qt/OvFrost.cpp)
    target_include_directories(over-blur-test PRIVATE ../Source/ui/qt)
    target_link_libraries(over-blur-test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
    set_target_properties(over-blur-test PROPERTIES AUTOMOC ON)

    # headless proof of the meter interpolation: smootherstep's C2 ends,
    # Catmull-Rom not overshooting, and a fall that takes the same time at
    # 15 Hz as at 120 Hz.  No Qt, no audio, no window -- it is arithmetic.
    add_executable(over-ballistics-test ../Tools/units/ballistics_test.cpp)
    target_include_directories(over-ballistics-test PRIVATE ../Source/ui/qt)

    # headless proof that the analyser is smooth: a steady signal's outline must
    # not move more than ~1 dB a frame, and the old 1/6-octave/dB-domain chain is
    # built beside it for comparison.  Same conditions, same source, so the two
    # numbers are a measurement rather than a memory.
    add_executable(over-spectrum-test ../Tools/units/spectrum_test.cpp)
    target_include_directories(over-spectrum-test PRIVATE ../Source/ui/qt)
    endif()
endif()

# Platform switch used by the sources: Windows vs. everything unix-like
# (Linux, macOS, BSD). Mirrors JUCE_WINDOWS and costs nothing.
if(NOT WIN32)
    set(OVERTEON_UNIXLIKE TRUE)
else()
    # MSVC's <cmath> hides M_PI unless this is defined, and the ballistics and
    # spectrum code uses it directly.  Defining it here rather than in each
    # source keeps the arithmetic identical on both platforms.
    add_compile_definitions(_USE_MATH_DEFINES)
    # Windows headers are noisy about the CRT's "unsafe" functions; the port
    # does not use any of them, but JUCE and the SDK headers do.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
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

target_compile_definitions(Overteon PUBLIC
    JUCE_WEB_BROWSER=${OVERTEON_WEB_BROWSER}
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    $<$<NOT:$<PLATFORM_ID:Windows>>:BUILD_UNIXLIKE=1>
    $<$<AND:$<PLATFORM_ID:Windows>,$<NOT:$<BOOL:${OVERTEON_QT_UI}>>>:JUCE_USE_WIN_WEBVIEW2=1>
    $<$<AND:$<PLATFORM_ID:Windows>,$<NOT:$<BOOL:${OVERTEON_QT_UI}>>>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>)

target_link_libraries(Overteon
    PRIVATE
        OverteonWebAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
