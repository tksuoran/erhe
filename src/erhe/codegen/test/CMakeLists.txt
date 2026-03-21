set(_target "erhe_codegen_test")
set(_codegen_out "${CMAKE_CURRENT_BINARY_DIR}/codegen")

add_executable(${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    test_migration.cpp
)

# Generated sources (listed explicitly so CMake knows about them)
set(_generated_sources
    ${_codegen_out}/settings.hpp
    ${_codegen_out}/settings_serialization.hpp
    ${_codegen_out}/settings.cpp
)
set_source_files_properties(${_generated_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_generated_sources})

target_include_directories(${_target}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${_codegen_out}
)

target_link_libraries(${_target}
    PRIVATE
        erhe::codegen
)

erhe_target_settings(${_target} "erhe")

# Wire up code generation
erhe_codegen_generate(
    TARGET          ${_target}
    DEFINITIONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/definitions"
    OUTPUT_DIR      "${_codegen_out}"
)
