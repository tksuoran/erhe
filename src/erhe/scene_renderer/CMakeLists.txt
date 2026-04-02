set(_target "erhe_scene_renderer")
add_library(${_target})
add_library(erhe::scene_renderer ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_scene_renderer/camera_buffer.cpp
    erhe_scene_renderer/camera_buffer.hpp
    erhe_scene_renderer/content_wide_line_renderer.cpp
    erhe_scene_renderer/content_wide_line_renderer.hpp
    erhe_scene_renderer/cube_instance_buffer.cpp
    erhe_scene_renderer/cube_instance_buffer.hpp
    erhe_scene_renderer/cube_renderer.cpp
    erhe_scene_renderer/cube_renderer.hpp
    erhe_scene_renderer/forward_renderer.cpp
    erhe_scene_renderer/forward_renderer.hpp
    erhe_scene_renderer/joint_buffer.cpp
    erhe_scene_renderer/joint_buffer.hpp
    erhe_scene_renderer/light_buffer.cpp
    erhe_scene_renderer/light_buffer.hpp
    erhe_scene_renderer/material_buffer.cpp
    erhe_scene_renderer/material_buffer.hpp
    erhe_scene_renderer/primitive_buffer.cpp
    erhe_scene_renderer/primitive_buffer.hpp
    erhe_scene_renderer/program_interface.cpp
    erhe_scene_renderer/program_interface.hpp
    erhe_scene_renderer/scene_renderer_log.cpp
    erhe_scene_renderer/scene_renderer_log.hpp
    erhe_scene_renderer/shadow_renderer.cpp
    erhe_scene_renderer/shadow_renderer.hpp
    erhe_scene_renderer/texel_renderer.cpp
    erhe_scene_renderer/texel_renderer.hpp
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
    PRIVATE
        erhe::file
        erhe::log
        erhe::message_bus
        erhe::profile
)

# Primitive_color_source enum codegen
set(_scene_renderer_defs "${CMAKE_CURRENT_SOURCE_DIR}/definitions")
set(_scene_renderer_gen  "${CMAKE_CURRENT_BINARY_DIR}/erhe_scene_renderer/generated")

erhe_codegen_generate(
    TARGET          ${_target}
    DEFINITIONS_DIR "${_scene_renderer_defs}"
    OUTPUT_DIR      "${_scene_renderer_gen}"
    DEFINITIONS
        "${_scene_renderer_defs}/primitive_color_source.py"
)

# Only the enum header -- serialize/deserialize .cpp compiles in the editor
set_source_files_properties(${_scene_renderer_gen}/primitive_color_source.hpp PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_scene_renderer_gen}/primitive_color_source.hpp)
source_group("codegen generated" FILES ${_scene_renderer_gen}/primitive_color_source.hpp)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

erhe_target_settings(${_target} "erhe")
