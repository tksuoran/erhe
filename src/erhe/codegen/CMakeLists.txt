set(_target "erhe_codegen")
add_library(${_target})
add_library(erhe::codegen ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_codegen/config_io.hpp
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
    PRIVATE
        erhe::file
)

erhe_target_settings(${_target} "erhe")

# erhe_codegen_generate() is defined in erhe_codegen_generate.cmake,
# which is included early by the parent CMakeLists.txt so that all
# targets (including those listed before codegen) can use it.

add_subdirectory(test)
