set(_target "erhe_renderer")
add_library(${_target})
add_library(erhe::renderer ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_renderer/debug_renderer.cpp
    erhe_renderer/debug_renderer.hpp
    erhe_renderer/debug_renderer_bucket.cpp
    erhe_renderer/debug_renderer_bucket.hpp
    erhe_renderer/draw_indirect_buffer.cpp
    erhe_renderer/draw_indirect_buffer.hpp
    erhe_renderer/primitive_renderer.cpp
    erhe_renderer/primitive_renderer.hpp
    erhe_renderer/renderer_log.cpp
    erhe_renderer/renderer_log.hpp
    erhe_renderer/text_renderer.cpp
    erhe_renderer/text_renderer.hpp
)
if (${ERHE_PHYSICS_LIBRARY} STREQUAL "jolt")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_renderer/jolt_debug_renderer.cpp
        erhe_renderer/jolt_debug_renderer.hpp
    )
    target_link_libraries(${_target} PUBLIC Jolt)
endif ()

target_link_libraries(
    ${_target}
    PUBLIC
        etl::etl
        erhe::codegen
        erhe::graphics
        erhe::math
        erhe::primitive
        erhe::scene
        erhe::ui
    PRIVATE
        erhe::utility
        erhe::defer
        erhe::log
        erhe::profile
        erhe::verify
)

# Visualization_mode enum codegen
set(_renderer_defs "${CMAKE_CURRENT_SOURCE_DIR}/definitions")
set(_renderer_gen  "${CMAKE_CURRENT_BINARY_DIR}/erhe_renderer/generated")

erhe_codegen_generate(
    TARGET          ${_target}
    DEFINITIONS_DIR "${_renderer_defs}"
    OUTPUT_DIR      "${_renderer_gen}"
    DEFINITIONS
        "${_renderer_defs}/visualization_mode.py"
)

# Only the enum header -- serialize/deserialize .cpp compiles in the editor
set_source_files_properties(${_renderer_gen}/visualization_mode.hpp PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_renderer_gen}/visualization_mode.hpp)
source_group("codegen generated" FILES ${_renderer_gen}/visualization_mode.hpp)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

erhe_target_settings(${_target} "erhe")
