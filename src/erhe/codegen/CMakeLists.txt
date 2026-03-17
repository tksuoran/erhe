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
#
# Usage:
#   erhe_codegen_generate(
#       TARGET          <target>           # CMake target that depends on generated code
#       DEFINITIONS_DIR <path>             # Directory containing .py definition files
#       OUTPUT_DIR      <path>             # Directory where .hpp/.cpp files are generated
#   )
#
# The caller must add the generated source files to their target and add
# OUTPUT_DIR to the target's include directories.
function(erhe_codegen_generate)
    cmake_parse_arguments(ARG "" "TARGET;DEFINITIONS_DIR;OUTPUT_DIR" "" ${ARGN})

    file(GLOB DEFINITION_FILES CONFIGURE_DEPENDS "${ARG_DEFINITIONS_DIR}/*.py")

    set(_codegen_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    set(GENERATOR_SCRIPT "${_codegen_dir}/generate.py")
    file(GLOB GENERATOR_PACKAGE_FILES CONFIGURE_DEPENDS "${_codegen_dir}/erhe_codegen/*.py")

    add_custom_command(
        OUTPUT ${ARG_OUTPUT_DIR}/generated_stamp.txt
        COMMAND ${Python3_EXECUTABLE} ${GENERATOR_SCRIPT}
            ${ARG_DEFINITIONS_DIR}
            ${ARG_OUTPUT_DIR}
        COMMAND ${CMAKE_COMMAND} -E touch ${ARG_OUTPUT_DIR}/generated_stamp.txt
        DEPENDS ${DEFINITION_FILES} ${GENERATOR_SCRIPT} ${GENERATOR_PACKAGE_FILES}
        COMMENT "erhe_codegen: generating C++ from ${ARG_DEFINITIONS_DIR}"
    )

    add_custom_target(${ARG_TARGET}_codegen DEPENDS ${ARG_OUTPUT_DIR}/generated_stamp.txt)
    add_dependencies(${ARG_TARGET} ${ARG_TARGET}_codegen)
endfunction()

add_subdirectory(test)
