cmake_minimum_required(VERSION 3.22)
project(Overteon VERSION 0.9.14)

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

juce_add_plugin(HarmonicInjector
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
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME "Overteon")

target_sources(HarmonicInjector PRIVATE
    src/FxProcessor.cpp
    src/FxWebEditor.cpp)

# Platform switch used by the sources: Windows vs. everything unix-like
# (Linux, macOS, BSD). Mirrors JUCE_WINDOWS and costs nothing.
if(NOT WIN32)
    set(OVERTEON_UNIXLIKE TRUE)
endif()

target_compile_definitions(HarmonicInjector PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    $<$<NOT:$<PLATFORM_ID:Windows>>:BUILD_UNIXLIKE=1>
    $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2=1>
    $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>)

target_link_libraries(HarmonicInjector
    PRIVATE
        FxWebAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
