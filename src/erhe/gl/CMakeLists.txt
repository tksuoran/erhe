set(_target "erhe_gl")

find_package(Python3 COMPONENTS Interpreter)
if (NOT ${Python3_FOUND})
    message(FATAL_ERROR "Python3 is needed to generate sources.")
endif ()

set(CMAKE_EXPORT_COMPILE_COMMANDS True)
set(GENERATED_SOURCES
    command_info.cpp
    command_info.hpp
    dynamic_load.cpp
    dynamic_load.hpp
    enum_base_zero_functions.cpp
    enum_base_zero_functions.hpp
    enum_string_functions.hpp
    enum_string_functions.cpp
    wrapper_enums.hpp
    wrapper_functions.hpp
    wrapper_functions.cpp
)

list(TRANSFORM GENERATED_SOURCES PREPEND "${CMAKE_CURRENT_BINARY_DIR}/")

set_source_files_properties(${GENERATED_SOURCES} GENERATED)

set(GEN_COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/generate_sources.py"
    "${CMAKE_CURRENT_BINARY_DIR}")

set(GEN_DEPENDS
    generate_sources.py
    gl.xml
    gl_extra.xml
    templates.py
    templates/autogeneration_warning.inl
    templates/basic_enum_string_function_definition.inl
    templates/bitmask_enum_string_function_definition.inl
    templates/bitmask_enum_string_make_entry.inl
    templates/command_info.cpp
    templates/command_info.hpp
    templates/dynamic_load.cpp
    templates/dynamic_load.hpp
    templates/enum_base_zero_function_definition.inl
    templates/enum_base_zero_functions.cpp
    templates/enum_base_zero_functions.hpp
    templates/enum_string_functions.cpp
    templates/enum_string_functions.hpp
    templates/untyped_enum_string_function_definition.inl
    templates/wrapper_enum_declaration.inl
    templates/wrapper_enums.hpp
    templates/wrapper_function_definition.inl
    templates/wrapper_functions.hpp
    templates/wrapper_functions.cpp
)

add_custom_command(
    COMMAND ${GEN_COMMAND}
    DEPENDS ${GEN_DEPENDS}
    OUTPUT  ${GENERATED_SOURCES}
)

set(source_list
    gl.cpp
    gl.hpp
    strong_gl_enums.hpp
    generate_sources.Pyt
)

list(APPEND source_list ${GEN_DEPENDS})

add_library(${_target}
    ${GENERATED_SOURCES}
    gl.cpp
)

target_include_directories(${_target}
    PUBLIC
        ${ERHE_INCLUDE_ROOT}
        ${PROJECT_BINARY_DIR}/src
        ${ERHE_ADDITIONAL_GL_INCLUDES}
    }
)

target_link_libraries(${_target}
    PUBLIC
        fmt::fmt
        Microsoft.GSL::GSL
        OpenGL::GL
        erhe_log
)

if (${ERHE_WINDOW_LIBRARY} STREQUAL "mango")
    target_link_libraries(${_target}
        PUBLIC
            mango
            mango-opengl
    )
endif ()

foreach (source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach ()
