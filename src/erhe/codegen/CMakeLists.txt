set(_target "erhe_codegen")
add_library(${_target})
add_library(erhe::codegen ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_codegen/field_info.hpp
    erhe_codegen/migration.hpp
    erhe_codegen/migration.cpp
    erhe_codegen/serialize_helpers.hpp
    erhe_codegen/serialize_helpers.cpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        simdjson::simdjson
        glm::glm-header-only
)

erhe_target_settings(${_target} "erhe")

# Code generation function for consumers
function(erhe_codegen_generate)
    cmake_parse_arguments(ARG "" "TARGET;DEFINITIONS_DIR;OUTPUT_DIR" "" ${ARGN})

    file(GLOB DEFINITION_FILES "${ARG_DEFINITIONS_DIR}/*.py")

    set(GENERATOR_SCRIPT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/generate.py")

    add_custom_command(
        OUTPUT ${ARG_OUTPUT_DIR}/generated_stamp.txt
        COMMAND ${Python3_EXECUTABLE} ${GENERATOR_SCRIPT}
            ${ARG_DEFINITIONS_DIR}
            ${ARG_OUTPUT_DIR}
        COMMAND ${CMAKE_COMMAND} -E touch ${ARG_OUTPUT_DIR}/generated_stamp.txt
        DEPENDS ${DEFINITION_FILES} ${GENERATOR_SCRIPT}
        COMMENT "Generating C++ code from struct definitions"
    )

    add_custom_target(${ARG_TARGET}_codegen DEPENDS ${ARG_OUTPUT_DIR}/generated_stamp.txt)
    add_dependencies(${ARG_TARGET} ${ARG_TARGET}_codegen)
endfunction()
