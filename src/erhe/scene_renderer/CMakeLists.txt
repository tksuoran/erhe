set(_target "erhe_scene_renderer")
add_library(${_target})
add_library(erhe::scene_renderer ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_scene_renderer/camera_buffer.cpp
    erhe_scene_renderer/camera_buffer.hpp
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
)
target_link_libraries(
    ${_target}
    PUBLIC
        erhe::dataformat
        erhe::graphics
        erhe::math
        erhe::primitive
        erhe::renderer
        erhe::scene
    PRIVATE
        erhe::file
        erhe::gl
        erhe::log
        erhe::message_bus
        erhe::profile
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
