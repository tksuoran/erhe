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

find_package(Python3 COMPONENTS Interpreter)
if (NOT ${Python3_FOUND})
    message(FATAL_ERROR "Python3 is needed for erhe_codegen code generation.")
endif()
# Cache the Python executable so the function below (and callers) can access it
set(ERHE_PYTHON3_EXECUTABLE "${Python3_EXECUTABLE}" CACHE INTERNAL "Python3 interpreter for erhe_codegen")

# Code generation function for consumers
#
# Usage:
#   erhe_codegen_generate(
#       TARGET          <target>           # CMake target that depends on generated code
#       DEFINITIONS_DIR <path>             # Directory containing .py definition files
#       OUTPUT_DIR      <path>             # Directory where .hpp/.cpp files are generated
#   )
#
# Runs the generator at configure time (like erhe_gl), so no build-time
# dependency on Python. Re-configure to regenerate after changing definitions.
function(erhe_codegen_generate)
    cmake_parse_arguments(ARG "" "TARGET;DEFINITIONS_DIR;OUTPUT_DIR" "" ${ARGN})

    set(_codegen_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    set(GENERATOR_SCRIPT "${_codegen_dir}/generate.py")

    message(STATUS "erhe_codegen: generating C++ from ${ARG_DEFINITIONS_DIR}")
    execute_process(
        COMMAND "${ERHE_PYTHON3_EXECUTABLE}" "${GENERATOR_SCRIPT}"
            "${ARG_DEFINITIONS_DIR}"
            "${ARG_OUTPUT_DIR}"
        RESULT_VARIABLE _codegen_result
        OUTPUT_VARIABLE _codegen_output
        ERROR_VARIABLE  _codegen_error
    )
    if (_codegen_result)
        message(FATAL_ERROR "erhe_codegen generation failed (exit code ${_codegen_result}):\nstdout: ${_codegen_output}\nstderr: ${_codegen_error}\nCommand: ${ERHE_PYTHON3_EXECUTABLE} ${GENERATOR_SCRIPT} ${ARG_DEFINITIONS_DIR} ${ARG_OUTPUT_DIR}")
    endif()
endfunction()

add_subdirectory(test)
