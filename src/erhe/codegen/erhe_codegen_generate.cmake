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
#       TARGET               <target>           # CMake target that depends on generated code
#       DEFINITIONS_DIR      <path>             # Directory containing .py definition files
#       OUTPUT_DIR           <path>             # Directory where .hpp/.cpp files are generated
#       DEFINITIONS          <file1> <file2>... # Python definition files (IDE grouping only;
#                                                 dependencies come from globbing DEFINITIONS_DIR)
#       OUTPUTS              <file1> <file2>... # Generated files this unit's target compiles.
#                                                 Declaring them as custom-command outputs makes
#                                                 ninja recompile their dependents in the SAME
#                                                 build that regenerated them; without this the
#                                                 rewritten file is an output of no edge, its
#                                                 start-of-build mtime stands, and dependents go
#                                                 stale until the next ninja invocation. Custom
#                                                 commands run with ninja restat, so outputs the
#                                                 generator leaves unchanged do not cascade.
#       EXTRA_DEFINITIONS_DIRS <dir1:prefix1> ... # Optional extra dirs for reference resolution only
#                                                  # Format: "path:include_prefix" or just "path"
#   )
#
# Runs the generator at configure time so files exist for the build system,
# AND adds a build-time custom command that re-runs when .py files change.
# Extra definition directories are loaded for StructRef/EnumRef resolution
# but their types are NOT generated into the output directory.
# Usage:
#   erhe_codegen_source_settings(<file> ...)
#
# Marks erhe_codegen output as GENERATED and, on MSVC, compiles the generated
# translation units unoptimized and without the shared precompiled header.
#
# The generated serialization units are long straight-line JSON glue plus a
# large static Field_info table per struct. MSVC's optimizer cost grows
# super-linearly in that shape: at /O2 a single 2500-line unit
# (transform_tool_config.cpp) took 4m04s to compile, and five such units made
# up 8 minutes of a 13m30s full rebuild -- a serial tail with 21 of 24 cores
# idle. The code runs only when a config is loaded or saved, so optimizing it
# buys nothing measurable. /Od took the editor target from ~11 min to ~2m20s.
#
# SKIP_PRECOMPILE_HEADERS is required rather than cosmetic: erhe_pch is built
# at /O2, and MSVC answers a mismatched optimization level in a unit that
# consumes that PCH with C4653 "current command-line option ignored" -- the
# /Od is silently dropped, and /WX turns the warning into a hard error. The
# generated units include little beyond their own headers and simdjson, so
# losing the PCH costs them very little (62 config units compile in 19s).
#
# Call this from the directory that COMPILES the files: source file properties
# are directory-scoped, and several libraries generate serialization .cpp that
# is compiled in a consuming target's directory instead (see the
# "compiles in the editor" notes at those call sites).
function(erhe_codegen_source_settings)
    set_source_files_properties(${ARGN} PROPERTIES GENERATED TRUE)

    if (NOT MSVC)
        return()
    endif()

    # Headers appear in these lists too; compile options belong on the units.
    set(_translation_units "")
    foreach (_file IN LISTS ARGN)
        if (_file MATCHES "\\.(c|cc|cpp|cxx)$")
            list(APPEND _translation_units "${_file}")
        endif()
    endforeach()

    if (_translation_units)
        set_source_files_properties(
            ${_translation_units} PROPERTIES
            COMPILE_OPTIONS         "/Od"
            SKIP_PRECOMPILE_HEADERS TRUE
        )
    endif()
endfunction()

function(erhe_codegen_generate)
    cmake_parse_arguments(ARG "" "TARGET;DEFINITIONS_DIR;OUTPUT_DIR" "DEFINITIONS;OUTPUTS;EXTRA_DEFINITIONS_DIRS" ${ARGN})

    # Guard: detect stale generated files left in (or accidentally copied into)
    # the source tree at the mirror of OUTPUT_DIR. Consumers typically include
    # via "<lib>/generated/<name>.hpp" and both the source and build dirs are
    # on the include path; the source dir is usually listed first, so stale
    # in-tree files silently shadow freshly generated output.
    string(FIND "${ARG_OUTPUT_DIR}" "${CMAKE_CURRENT_BINARY_DIR}" _bin_prefix_idx)
    if (_bin_prefix_idx EQUAL 0)
        string(REPLACE
            "${CMAKE_CURRENT_BINARY_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}"
            _stale_mirror
            "${ARG_OUTPUT_DIR}")
        if (EXISTS "${_stale_mirror}")
            message(FATAL_ERROR
                "erhe_codegen: stale generated directory exists in source tree:\n"
                "    ${_stale_mirror}\n"
                "These files shadow freshly generated output in "
                "${ARG_OUTPUT_DIR}. Delete the stale directory and reconfigure.")
        endif()
    endif()

    set(_codegen_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    set(GENERATOR_SCRIPT "${_codegen_dir}/generate.py")

    # Run at configure time so generated files exist immediately
    message(STATUS "erhe_codegen: generating C++ from ${ARG_DEFINITIONS_DIR}")
    execute_process(
        COMMAND "${ERHE_PYTHON3_EXECUTABLE}" "${GENERATOR_SCRIPT}"
            "${ARG_DEFINITIONS_DIR}"
            "${ARG_OUTPUT_DIR}"
            ${ARG_EXTRA_DEFINITIONS_DIRS}
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

    # Build-time command: re-runs generator when any .py file changes. The
    # generator scans the whole DEFINITIONS_DIR, so depend on a glob of it
    # (CONFIGURE_DEPENDS) rather than the explicit DEFINITIONS list - a newly
    # added definition file must become a dependency without a manual edit.
    file(GLOB _definition_files CONFIGURE_DEPENDS "${ARG_DEFINITIONS_DIR}/*.py")
    if (NOT ARG_DEFINITIONS)
        set(ARG_DEFINITIONS ${_definition_files})
    endif()
    # The always-touched stamp keeps the command clean between runs even
    # though the generator leaves unchanged output files' mtimes alone.
    set(_stamp "${ARG_OUTPUT_DIR}/.codegen_stamp")
    add_custom_command(
        OUTPUT  "${_stamp}" ${ARG_OUTPUTS}
        COMMAND "${ERHE_PYTHON3_EXECUTABLE}" "${GENERATOR_SCRIPT}"
                "${ARG_DEFINITIONS_DIR}"
                "${ARG_OUTPUT_DIR}"
                ${ARG_EXTRA_DEFINITIONS_DIRS}
        COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
        DEPENDS ${_definition_files} ${_gen_files}
        COMMENT "erhe_codegen: regenerating from ${ARG_DEFINITIONS_DIR}"
    )
    set_source_files_properties("${_stamp}" PROPERTIES GENERATED TRUE)
    target_sources(${ARG_TARGET} PRIVATE "${_stamp}" ${ARG_DEFINITIONS})

    # Organize in IDE: definitions go under their source tree, stamp is hidden
    source_group(TREE "${ARG_DEFINITIONS_DIR}" PREFIX "codegen definitions" FILES ${ARG_DEFINITIONS})
    source_group("codegen" FILES "${_stamp}")
endfunction()
