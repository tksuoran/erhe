# erhe_codegen_generate.cmake
#
# Provides the erhe_codegen_generate() function for consumers.
# Include this file before any add_subdirectory() that calls erhe_codegen_generate().

find_package(Python3 COMPONENTS Interpreter)
if (NOT ${Python3_FOUND})
    message(FATAL_ERROR "Python3 is needed for erhe_codegen code generation.")
endif()
set(ERHE_PYTHON3_EXECUTABLE "${Python3_EXECUTABLE}" CACHE INTERNAL "Python3 interpreter for erhe_codegen")

# Usage:
#   erhe_codegen_generate(
#       TARGET          <target>           # CMake target that depends on generated code
#       DEFINITIONS_DIR <path>             # Directory containing .py definition files
#       OUTPUT_DIR      <path>             # Directory where .hpp/.cpp files are generated
#       DEFINITIONS     <file1> <file2>... # Python definition files (listed explicitly)
#   )
#
# Runs the generator at configure time so files exist for the build system,
# AND adds a build-time custom command that re-runs when .py files change.
function(erhe_codegen_generate)
    cmake_parse_arguments(ARG "" "TARGET;DEFINITIONS_DIR;OUTPUT_DIR" "DEFINITIONS" ${ARGN})

    set(_codegen_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    set(GENERATOR_SCRIPT "${_codegen_dir}/generate.py")

    # Run at configure time so generated files exist immediately
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

    # Generator source files (listed explicitly -- no globbing)
    set(_gen_files
        "${_codegen_dir}/generate.py"
        "${_codegen_dir}/erhe_codegen/__init__.py"
        "${_codegen_dir}/erhe_codegen/emit_cpp.py"
        "${_codegen_dir}/erhe_codegen/emit_enum.py"
        "${_codegen_dir}/erhe_codegen/emit_hpp.py"
        "${_codegen_dir}/erhe_codegen/emit_reflect.py"
        "${_codegen_dir}/erhe_codegen/schema.py"
        "${_codegen_dir}/erhe_codegen/types.py"
    )

    # Build-time command: re-runs generator when any .py file changes
    set(_stamp "${ARG_OUTPUT_DIR}/.codegen_stamp")
    add_custom_command(
        OUTPUT  "${_stamp}"
        COMMAND "${ERHE_PYTHON3_EXECUTABLE}" "${GENERATOR_SCRIPT}"
                "${ARG_DEFINITIONS_DIR}"
                "${ARG_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
        DEPENDS ${ARG_DEFINITIONS} ${_gen_files}
        COMMENT "erhe_codegen: regenerating from ${ARG_DEFINITIONS_DIR}"
    )
    set_source_files_properties("${_stamp}" PROPERTIES GENERATED TRUE)
    target_sources(${ARG_TARGET} PRIVATE "${_stamp}" ${ARG_DEFINITIONS})

    # Organize in IDE: definitions go under their source tree, stamp is hidden
    source_group(TREE "${ARG_DEFINITIONS_DIR}" PREFIX "codegen definitions" FILES ${ARG_DEFINITIONS})
    source_group("codegen" FILES "${_stamp}")
endfunction()
