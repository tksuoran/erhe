message(STATUS "Configuring executable 'rendering_test'.")
set(_target "rendering_test")
add_executable(${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${PROJECT_SOURCE_DIR}" FILES
    config/rendering_test/erhe_graphics.json
    config/rendering_test/logging.json
    config/rendering_test/mesh_memory.json
    config/rendering_test/settings.json
    config/rendering_test/scene_wideline_scene.json
    config/rendering_test/wideline_scene_wideline.json
    config/rendering_test/cube_sphere.json
    config/rendering_test/sphere_cube.json
    config/rendering_test/cube_sphere_masked.json
    config/rendering_test/sphere_cube_masked.json
    config/rendering_test/pipeline_switch_AB.json
    config/rendering_test/pipeline_switch_BA.json
    config/rendering_test/pipeline_switch_AC.json
    config/rendering_test/pipeline_switch_CA.json
    config/rendering_test/wideline_pipe_switch.json
    config/rendering_test/wel_then_stencil_wl.json
    config/rendering_test/minimal_compute_AB.json
    config/rendering_test/minimal_compute_BA.json
    config/rendering_test/minimal_compute_CD.json
    config/rendering_test/minimal_stw1_then_D.json
    config/rendering_test/minimal_C_stw1_D.json
    config/rendering_test/minimal_C_and_stw1_then_D.json
    config/rendering_test/window.json

    res/shaders/standard.vert
    res/shaders/standard.frag
    res/rendering_test/shaders/textured_quad.vert
    res/rendering_test/shaders/textured_quad.frag
    res/rendering_test/shaders/multi_texture_test.vert
    res/rendering_test/shaders/multi_texture_test.frag
    res/rendering_test/shaders/separate_samplers_test.frag
    res/rendering_test/shaders/stencil_color.vert
    res/rendering_test/shaders/stencil_color.frag
    res/rendering_test/shaders/compute_before_content_line.comp
    res/rendering_test/shaders/minimal_compute_triangle.comp
    res/rendering_test/shaders/minimal_triangle.vert
    res/rendering_test/shaders/minimal_triangle.frag
    res/rendering_test/shaders/line_after_compute.vert
    res/rendering_test/shaders/line_after_compute.frag
    res/rendering_test/shaders/content_line_after_compute.frag
)

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    main.cpp
    programs.cpp
    programs.hpp
    rendering_test.cpp
    rendering_test.hpp
    rendering_test_app.hpp
    rendering_test_setup.cpp
    rendering_test_log.cpp
    rendering_test_log.hpp
    cell_scene.cpp
    cell_textured_quad.cpp
    cell_multi_texture.cpp
    cell_separate_samplers.cpp
    cell_depth_visualize.cpp
    cell_stencil.cpp
    cell_stencil_wide_line.cpp
    cell_minimal_compute_triangle.cpp
)

# Graphics_config serialize/deserialize must compile here (same target as
# other simdjson users) to avoid ABI mismatch from differing arch flags.
set(_gfx_gen "${CMAKE_BINARY_DIR}/src/erhe/graphics/erhe_graphics/generated")
set(_gfx_ser_sources
    ${_gfx_gen}/graphics_config_serialization.hpp
    ${_gfx_gen}/graphics_config.cpp
    ${_gfx_gen}/opengl_config_serialization.hpp
    ${_gfx_gen}/opengl_config.cpp
    ${_gfx_gen}/vulkan_config_serialization.hpp
    ${_gfx_gen}/vulkan_config.cpp
)
set_source_files_properties(${_gfx_ser_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_gfx_ser_sources})
source_group("codegen generated/graphics" FILES ${_gfx_ser_sources})

set(_scene_renderer_gen "${CMAKE_BINARY_DIR}/src/erhe/scene_renderer/erhe_scene_renderer/generated")
set(_scene_renderer_ser_sources
    ${_scene_renderer_gen}/mesh_memory_config_serialization.hpp
    ${_scene_renderer_gen}/mesh_memory_config.cpp
)
set_source_files_properties(${_scene_renderer_ser_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_scene_renderer_ser_sources})
source_group("codegen generated/scene_renderer" FILES ${_scene_renderer_ser_sources})

# Rendering test settings codegen (struct + serialization live in this target only)
set(_rt_config_defs "${CMAKE_CURRENT_SOURCE_DIR}/definitions")
set(_rt_config_out  "${CMAKE_CURRENT_BINARY_DIR}/generated/rendering_test")

erhe_codegen_generate(
    TARGET          ${_target}
    DEFINITIONS_DIR "${_rt_config_defs}"
    OUTPUT_DIR      "${_rt_config_out}"
    DEFINITIONS
        "${_rt_config_defs}/rendering_test_settings.py"
)

set(_rt_ser_sources
    ${_rt_config_out}/rendering_test_settings.hpp
    ${_rt_config_out}/rendering_test_settings_serialization.hpp
    ${_rt_config_out}/rendering_test_settings.cpp
)
set_source_files_properties(${_rt_ser_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_rt_ser_sources})
source_group("codegen generated/rendering_test" FILES ${_rt_ser_sources})

target_include_directories(${_target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Required for log init
if (ERHE_GRAPHICS_API_OPENGL)
    target_link_libraries(${_target} PRIVATE erhe::gl)
endif ()

if (ERHE_USE_MIMALLOC)
    target_link_libraries(${_target} PUBLIC mimalloc-static)
endif ()

target_link_libraries(
    ${_target}
    PRIVATE
        Taskflow
        simdjson
        erhe::codegen
        erhe::utility
        erhe::dataformat
        erhe::file
        erhe::geometry
        erhe::gltf
        erhe::graphics
        erhe::log
        erhe::math
        erhe::primitive
        erhe::profile
        erhe::raytrace
        erhe::renderer
        erhe::scene
        erhe::scene_renderer
        erhe::ui
        erhe::verify
        erhe::window
)

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

set_target_properties(
    ${_target} PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS        NO
)

erhe_target_settings(${_target} "erhe-executables")
