# Slang is the only authored shader language. Compile directly to SPIR-V and
# package it for Qt ShaderEffect without passing through a GLSL source file.
function(ov_configure_qt_shaders)
    find_program(OVERTEON_SLANGC NAMES slangc
        HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../Build/tools/slang/bin"
        DOC "Host Slang shader compiler")
    if(NOT OVERTEON_SLANGC)
        message(FATAL_ERROR "Slang compiler not found. Install slangc or set -DOVERTEON_SLANGC=/path/to/slangc")
    endif()

    # The packer uses Qt's private ShaderTools API on the BUILD host only.
    # A cross-build can supply a prebuilt packer instead of building one here.
    if(OVERTEON_SLANG_QSB)
        set(_packer "${OVERTEON_SLANG_QSB}")
        set(_packer_dep "${OVERTEON_SLANG_QSB}")
    elseif(CMAKE_CROSSCOMPILING)
        include(ExternalProject)
        set(OVERTEON_SHADER_HOST_QT "" CACHE PATH "Native Qt kit for the Slang QSB packer")
        set(_host_build "${CMAKE_CURRENT_BINARY_DIR}/slang-qsb-host")
        ExternalProject_Add(overteon_slang_qsb_host
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Tools/slang-qsb"
            BINARY_DIR "${_host_build}"
            CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
                       "-DCMAKE_PREFIX_PATH=${OVERTEON_SHADER_HOST_QT}"
            INSTALL_COMMAND "")
        set(_packer "${_host_build}/overteon-slang-qsb${CMAKE_HOST_EXECUTABLE_SUFFIX}")
        set(_packer_dep overteon_slang_qsb_host)
    else()
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Tools/slang-qsb"
                         "${CMAKE_CURRENT_BINARY_DIR}/slang-qsb")
        set(_packer "$<TARGET_FILE:overteon-slang-qsb>")
        set(_packer_dep overteon-slang-qsb)
    endif()

    set(_shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/../Source/ui/qt/shaders")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ov_shaders")
    set(_outputs "")
    foreach(_effect blur glass)
        foreach(_stage vert frag)
            if(_stage STREQUAL "vert")
                set(_entry vertexMain)
                set(_slang_stage vertex)
            else()
                set(_entry fragmentMain)
                set(_slang_stage fragment)
            endif()
            set(_name "${_effect}.${_stage}")
            set(_spv "${CMAKE_CURRENT_BINARY_DIR}/ov_shaders/${_name}.spv")
            set(_qsb "${CMAKE_CURRENT_BINARY_DIR}/ov_shaders/${_name}.qsb")
            add_custom_command(OUTPUT "${_qsb}"
                BYPRODUCTS "${_spv}"
                COMMAND "${OVERTEON_SLANGC}" "${_shader_dir}/${_effect}.slang"
                        -entry ${_entry} -stage ${_slang_stage} -target spirv
                        -profile spirv_1_0 -fvk-use-gl-layout -o "${_spv}"
                COMMAND "${_packer}" ${_stage} "${_spv}" "${_qsb}"
                DEPENDS "${_shader_dir}/${_effect}.slang"
                        "${_shader_dir}/effect.slang" "${_shader_dir}/gaussian.slang"
                        ${_packer_dep}
                COMMENT "Slang -> SPIR-V -> QSB: ${_name}"
                VERBATIM)
            list(APPEND _outputs "${_qsb}")
        endforeach()
    endforeach()
    add_custom_target(overteon_shaders DEPENDS ${_outputs})

    set(OVERTEON_SHADER_QRC "${CMAKE_CURRENT_BINARY_DIR}/ov_shaders/ov_shaders.qrc" PARENT_SCOPE)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ov_shaders/ov_shaders.qrc"
        "<RCC><qresource prefix=\"/shaders\">\n"
        "  <file alias=\"blur.vert.qsb\">blur.vert.qsb</file>\n"
        "  <file alias=\"blur.frag.qsb\">blur.frag.qsb</file>\n"
        "  <file alias=\"glass.vert.qsb\">glass.vert.qsb</file>\n"
        "  <file alias=\"glass.frag.qsb\">glass.frag.qsb</file>\n"
        "</qresource></RCC>\n")
endfunction()
