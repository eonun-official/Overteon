cmake_minimum_required(VERSION 3.22)
project(OverteonSlangQsb LANGUAGES CXX)

# This is a build-host tool, never linked into the plug-in. Configure it with
# the host Qt kit when cross-compiling the application.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui ShaderTools)
# Newer Qt distributions expose private modules as separate packages.
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6GuiPrivate REQUIRED)
endif()
if(NOT TARGET Qt6::ShaderToolsPrivate)
    find_package(Qt6ShaderToolsPrivate REQUIRED)
endif()
add_executable(overteon-slang-qsb main.cpp)
target_compile_features(overteon-slang-qsb PRIVATE cxx_std_17)
target_link_libraries(overteon-slang-qsb PRIVATE
    Qt6::Core Qt6::GuiPrivate Qt6::ShaderToolsPrivate)
