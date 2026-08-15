set(_target "erhe_scene_renderer")
add_library(${_target})
add_library(erhe::scene_renderer ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_scene_renderer/camera_buffer.cpp
    erhe_scene_renderer/camera_buffer.hpp
    erhe_scene_renderer/content_wide_line_interface.cpp
    erhe_scene_renderer/content_wide_line_interface.hpp
    erhe_scene_renderer/content_wide_line_compute_renderer.cpp
    erhe_scene_renderer/content_wide_line_geometry_renderer.cpp
    erhe_scene_renderer/content_wide_line_renderer.cpp
    erhe_scene_renderer/content_wide_line_renderer.hpp
    erhe_scene_renderer/content_wide_line_view_writer.hpp
    erhe_scene_renderer/cube_instance_buffer.cpp
    erhe_scene_renderer/cube_instance_buffer.hpp
    erhe_scene_renderer/cube_renderer.cpp
    erhe_scene_renderer/cube_renderer.hpp
    erhe_scene_renderer/draw_indirect_buffer.cpp
    erhe_scene_renderer/draw_indirect_buffer.hpp
    erhe_scene_renderer/forward_renderer.cpp
    erhe_scene_renderer/forward_renderer.hpp
    erhe_scene_renderer/glyph_buffer.cpp
    erhe_scene_renderer/glyph_buffer.hpp
    erhe_scene_renderer/joint_buffer.cpp
    erhe_scene_renderer/joint_buffer.hpp
    erhe_scene_renderer/light_buffer.cpp
    erhe_scene_renderer/light_buffer.hpp
    erhe_scene_renderer/material_buffer.cpp
    erhe_scene_renderer/material_buffer.hpp
    erhe_scene_renderer/mesh_memory.cpp
    erhe_scene_renderer/mesh_memory.hpp
    erhe_scene_renderer/buffer_pool.hpp
    erhe_scene_renderer/buffer_pool.cpp
    erhe_scene_renderer/primitive_buffer.cpp
    erhe_scene_renderer/primitive_buffer.hpp
    erhe_scene_renderer/program_interface.cpp
    erhe_scene_renderer/program_interface.hpp
    erhe_scene_renderer/scene_renderer_log.cpp
    erhe_scene_renderer/scene_renderer_log.hpp
    erhe_scene_renderer/shader_key.cpp
    erhe_scene_renderer/shader_key.hpp
    erhe_scene_renderer/shader_variant_cache.cpp
    erhe_scene_renderer/shader_variant_cache.hpp
    erhe_scene_renderer/shadow_renderer.cpp
    erhe_scene_renderer/shadow_renderer.hpp
    erhe_scene_renderer/texel_renderer.cpp
    erhe_scene_renderer/texel_renderer.hpp

    erhe_scene_renderer/draw_list_entry.cpp
    erhe_scene_renderer/draw_list_entry.hpp
    erhe_scene_renderer/draw_list_key.cpp
    erhe_scene_renderer/draw_list_key.hpp
    erhe_scene_renderer/draw_list_object.cpp
    erhe_scene_renderer/draw_list_object.hpp
    erhe_scene_renderer/draw_list_scene.cpp
    erhe_scene_renderer/draw_list_scene.hpp
    erhe_scene_renderer/draw_list.cpp
    erhe_scene_renderer/draw_list.hpp
)
target_link_libraries(
    ${_target}
    PUBLIC
        erhe::codegen
        erhe::dataformat
        erhe::graphics
        erhe::math
        erhe::primitive
        erhe::renderer
        erhe::scene
        erhe::ui
    PRIVATE
        erhe::file
        erhe::log
        erhe::message_bus
        erhe::profile
)

# Primitive_color_source enum codegen
set(_scene_renderer_defs "${CMAKE_CURRENT_SOURCE_DIR}/definitions")
set(_scene_renderer_gen  "${CMAKE_CURRENT_BINARY_DIR}/erhe_scene_renderer/generated")

# OUTPUTS: see erhe_codegen_generate.cmake - declaring the generated files as
# custom-command outputs makes ninja recompile dependents in the same build.
set(_scene_renderer_generated
    ${_scene_renderer_gen}/primitive_color_source.hpp
    ${_scene_renderer_gen}/primitive_color_source.cpp
    ${_scene_renderer_gen}/mesh_memory_config.hpp
    ${_scene_renderer_gen}/mesh_memory_config_serialization.hpp
    ${_scene_renderer_gen}/mesh_memory_config.cpp
)
erhe_codegen_generate(
    TARGET          ${_target}
    DEFINITIONS_DIR "${_scene_renderer_defs}"
    OUTPUT_DIR      "${_scene_renderer_gen}"
    OUTPUTS         ${_scene_renderer_generated}
)

# Only the enum header -- serialize/deserialize .cpp compiles in the editor
erhe_codegen_source_settings(${_scene_renderer_gen}/primitive_color_source.hpp)
target_sources(${_target} PRIVATE ${_scene_renderer_gen}/primitive_color_source.hpp)
source_group("codegen generated" FILES ${_scene_renderer_gen}/primitive_color_source.hpp)

# Mesh_memory_config struct -- header only here, serialization .cpp compiles in the editor
erhe_codegen_source_settings(${_scene_renderer_gen}/mesh_memory_config.hpp)
target_sources(${_target} PRIVATE ${_scene_renderer_gen}/mesh_memory_config.hpp)
source_group("codegen generated" FILES ${_scene_renderer_gen}/mesh_memory_config.hpp)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

erhe_target_settings(${_target} "erhe")
