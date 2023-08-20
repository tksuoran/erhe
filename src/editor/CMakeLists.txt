set(_target "editor")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES

    graphics/gradients.cpp
    graphics/gradients.hpp
    graphics/icon_rasterization.cpp
    graphics/icon_rasterization.hpp
    graphics/icon_set.cpp
    graphics/icon_set.hpp
    graphics/image_transfer.cpp
    graphics/image_transfer.hpp

    operations/compound_operation.cpp
    operations/compound_operation.hpp
    operations/geometry_operations.cpp
    operations/geometry_operations.hpp
    operations/insert_operation.cpp
    operations/insert_operation.hpp
    operations/ioperation.hpp
    operations/merge_operation.cpp
    operations/merge_operation.hpp
    operations/mesh_operation.cpp
    operations/mesh_operation.hpp
    operations/node_operation.cpp
    operations/node_operation.hpp
    operations/operation_stack.cpp
    operations/operation_stack.hpp

    parsers/json_polyhedron.cpp
    parsers/json_polyhedron.hpp
    parsers/wavefront_obj.cpp
    parsers/wavefront_obj.hpp

    renderers/composer.cpp
    renderers/composer.hpp
    renderers/frustum_tiler.cpp
    renderers/frustum_tiler.hpp
    renderers/id_renderer.cpp
    renderers/id_renderer.hpp
    renderers/light_mesh.cpp
    renderers/light_mesh.hpp
    renderers/mesh_memory.cpp
    renderers/mesh_memory.hpp
    renderers/programs.cpp
    renderers/programs.hpp
    renderers/render_context.cpp
    renderers/render_context.hpp
    renderers/render_style.cpp
    renderers/render_style.hpp
    renderers/renderpass.cpp
    renderers/renderpass.hpp
    renderers/viewport_config.cpp
    renderers/viewport_config.hpp

    rendergraph/basic_viewport_window.cpp
    rendergraph/basic_viewport_window.hpp
    rendergraph/post_processing.cpp
    rendergraph/post_processing.hpp
    rendergraph/shadow_render_node.cpp
    rendergraph/shadow_render_node.hpp

    scene/asset_browser.cpp
    scene/asset_browser.hpp
    scene/content_library.cpp
    scene/content_library.hpp
    scene/debug_draw.cpp
    scene/debug_draw.hpp
    scene/frame_controller.cpp
    scene/frame_controller.hpp
    scene/material_library.cpp
    scene/material_library.hpp
    scene/material_preview.cpp
    scene/material_preview.hpp
    scene/node_physics.cpp
    scene/node_physics.hpp
    scene/node_raytrace.cpp
    scene/node_raytrace.hpp
    scene/scene_builder.cpp
    scene/scene_builder.hpp
    scene/scene_commands.cpp
    scene/scene_commands.hpp
    #scene/scene_message_bus.cpp
    #scene/scene_message_bus.hpp
    scene/scene_root.cpp
    scene/scene_root.hpp
    scene/scene_view.cpp
    scene/scene_view.hpp
    scene/viewport_window.cpp
    scene/viewport_window.hpp
    scene/viewport_windows.cpp
    scene/viewport_windows.hpp

    tools/brushes/brush.cpp
    tools/brushes/brush.hpp
    tools/brushes/brush_tool.cpp
    tools/brushes/brush_tool.hpp
    tools/brushes/create/create.cpp
    tools/brushes/create/create.hpp
    tools/brushes/create/create_preview_settings.hpp
    tools/brushes/create/create_box.cpp
    tools/brushes/create/create_box.hpp
    tools/brushes/create/create_cone.cpp
    tools/brushes/create/create_cone.hpp
    tools/brushes/create/create_shape.cpp
    tools/brushes/create/create_shape.hpp
    tools/brushes/create/create_torus.cpp
    tools/brushes/create/create_torus.hpp
    tools/brushes/create/create_uv_sphere.cpp
    tools/brushes/create/create_uv_sphere.hpp
    tools/transform/handle_enums.cpp
    tools/transform/handle_enums.hpp
    tools/transform/handle_visualizations.cpp
    tools/transform/handle_visualizations.hpp
    tools/transform/move_tool.cpp
    tools/transform/move_tool.hpp
    tools/transform/rotate_tool.cpp
    tools/transform/rotate_tool.hpp
    tools/transform/rotation_inspector.cpp
    tools/transform/rotation_inspector.hpp
    tools/transform/scale_tool.cpp
    tools/transform/scale_tool.hpp
    tools/transform/subtool.cpp
    tools/transform/subtool.hpp
    tools/transform/transform_tool.cpp
    tools/transform/transform_tool.hpp
    tools/transform/transform_tool_settings.cpp
    tools/transform/transform_tool_settings.hpp
    tools/debug_visualizations.cpp
    tools/debug_visualizations.hpp
    tools/fly_camera_tool.cpp
    tools/fly_camera_tool.hpp
    tools/grid.cpp
    tools/grid.hpp
    tools/grid_tool.cpp
    tools/grid_tool.hpp
    tools/hotbar.cpp
    tools/hotbar.hpp
    tools/hover_tool.cpp
    tools/hover_tool.hpp
    tools/hud.cpp
    tools/hud.hpp
    tools/material_paint_tool.cpp
    tools/material_paint_tool.hpp
    tools/paint_tool.cpp
    tools/paint_tool.hpp
    tools/physics_tool.cpp
    tools/physics_tool.hpp
    tools/selection_tool.cpp
    tools/selection_tool.hpp
    tools/tool.cpp
    tools/tool.hpp
    tools/tools.cpp
    tools/tools.hpp

    windows/animation_curve.cpp
    windows/animation_curve.hpp
    windows/animation_window.cpp
    windows/animation_window.hpp
    windows/brdf_slice_window.cpp
    windows/brdf_slice_window.hpp
    windows/commands_window.cpp
    windows/commands_window.hpp
    windows/content_library_window.cpp
    windows/content_library_window.hpp
    windows/debug_view_window.cpp
    windows/debug_view_window.hpp
    windows/imgui_viewport_window.cpp
    windows/imgui_viewport_window.hpp
    windows/layers_window.cpp
    windows/layers_window.hpp
    windows/network_window.cpp
    windows/network_window.hpp
    windows/item_tree_window.cpp
    windows/item_tree_window.hpp
    windows/operations.cpp
    windows/operations.hpp
    windows/post_processing_window.cpp
    windows/post_processing_window.hpp
    windows/physics_window.cpp
    windows/physics_window.hpp
    windows/properties.cpp
    windows/properties.hpp
    windows/rendergraph_window.cpp
    windows/rendergraph_window.hpp
    windows/settings_window.cpp
    windows/settings_window.hpp
    windows/tool_properties_window.cpp
    windows/tool_properties_window.hpp
    windows/viewport_config_window.cpp
    windows/viewport_config_window.hpp

    editor.cpp
    editor.hpp
    editor_context.cpp
    editor_context.hpp
    editor_log.cpp
    editor_log.hpp
    editor_message.hpp
    editor_message_bus.cpp
    editor_message_bus.hpp
    editor_rendering.cpp
    editor_rendering.hpp
    editor_scenes.cpp
    editor_scenes.hpp
    editor_settings.cpp
    editor_settings.hpp
    editor_windows.cpp
    editor_windows.hpp
    input_state.cpp
    input_state.hpp
    main.cpp
    renderable.hpp
    rendertarget_mesh.cpp
    rendertarget_mesh.hpp
    rendertarget_imgui_viewport.cpp
    rendertarget_imgui_viewport.hpp
    task_queue.cpp
    task_queue.hpp
    time.cpp
    time.hpp

    res/icons/camera.svg
    res/icons/directional_light.svg
    res/icons/drag.svg
    res/icons/mesh.svg
    res/icons/move.svg
    res/icons/node.svg
    res/icons/point_light.svg
    res/icons/pull.svg
    res/icons/push.svg
    res/icons/rotate.svg
    res/icons/spot_light.svg

    res/shaders/anisotropic_engine_ready.frag
    res/shaders/anisotropic_engine_ready.vert
    res/shaders/anisotropic_slope.frag
    res/shaders/anisotropic_slope.vert
    res/shaders/brdf_slice.frag
    res/shaders/brdf_slice.vert
    res/shaders/brush.frag
    res/shaders/brush.vert
    res/shaders/circular_brushed_metal.frag
    res/shaders/circular_brushed_metal.vert
    res/shaders/compose.frag
    res/shaders/compute_before_line.comp
    res/shaders/depth.frag
    res/shaders/depth.vert
    res/shaders/downsample_x.frag
    res/shaders/downsample_y.frag
    res/shaders/edge_lines.frag
    res/shaders/edge_lines.vert
    res/shaders/error.frag
    res/shaders/error.vert
    res/shaders/fat_triangle.frag
    res/shaders/fat_triangle.geom
    res/shaders/fat_triangle.vert
    res/shaders/id.frag
    res/shaders/id.vert
    res/shaders/line_after_compute.frag
    res/shaders/line_after_compute.vert
    res/shaders/line.frag
    res/shaders/line.geom
    res/shaders/line.vert
    res/shaders/points.frag
    res/shaders/points.vert
    res/shaders/post_processing.vert
    res/shaders/sky.frag
    res/shaders/sky.vert
    res/shaders/standard_debug.frag
    res/shaders/standard_debug.vert
    res/shaders/standard.frag
    res/shaders/standard.vert
    res/shaders/text.frag
    res/shaders/text.vert
    res/shaders/textured.frag
    res/shaders/textured.vert
    res/shaders/tool.frag
    res/shaders/tool.vert
    res/shaders/visualize_depth.frag
    res/shaders/visualize_depth.vert
    res/shaders/wide_lines.frag
    res/shaders/wide_lines.geom
    res/shaders/wide_lines.vert

    erhe.ini
    logging.ini
    presets.ini
    #user.ini
    windows.ini
)

if (${ERHE_GLTF_LIBRARY} STREQUAL "cgltf")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        parsers/gltf.cpp
        parsers/gltf.hpp
    )
endif ()


if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        xr/controller_visualization.cpp
        xr/controller_visualization.hpp
        xr/hand_tracker.cpp
        xr/hand_tracker.hpp
        xr/headset_view.cpp
        xr/headset_view.hpp
        xr/headset_view_resources.cpp
        xr/headset_view_resources.hpp
        xr/theremin.cpp
        xr/theremin.hpp
    )
else ()
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        xr/null_headset_view.cpp
        xr/null_headset_view.hpp
    )
endif ()

target_link_libraries(
    ${_target}
    PRIVATE
    erhe::commands
    erhe::concurrency
    erhe::configuration
    erhe::geometry
    erhe::gl
    erhe::gltf
    erhe::graphics
    erhe::imgui
    erhe::log
    erhe::net
    erhe::physics
    erhe::primitive
    erhe::renderer
    erhe::rendergraph
    erhe::scene
    erhe::scene_renderer
    erhe::toolkit
    erhe::ui
    meshoptimizer
    mINI
    RectangleBinPack
    rapidjson
    Taskflow
)

if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
    target_link_libraries(${_target} PRIVATE imgui)
endif ()

if (${ERHE_SVG_LIBRARY} STREQUAL "lunasvg")
    target_link_libraries(${_target} PRIVATE lunasvg)
endif ()

if (${ERHE_AUDIO_LIBRARY} STREQUAL "miniaudio")
    target_link_libraries(${_target} PRIVATE miniaudio)
endif ()

if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    target_link_libraries(${_target} PRIVATE erhe_xr openxr_loader)
endif ()

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

if (${ERHE_GLTF_LIBRARY} STREQUAL "cgltf")
    target_link_libraries(${_target} PRIVATE cgltf)
endif ()

target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(
    ${_target} PROPERTIES
    CXX_STANDARD                  20
    CXX_STANDARD_REQUIRED         YES
    CXX_EXTENSIONS                NO
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")

########

set(_target "net-test")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    net_test.cpp
)

target_link_libraries(
    ${_target}
    PRIVATE
    erhe::log
    erhe::net
    erhe::toolkit
    cxxopts
    mINI
    RectangleBinPack
    rapidjson
)

if (${ERHE_TERMINAL_LIBRARY} STREQUAL "cpp-terminal")
    target_link_libraries(${_target} PRIVATE cpp-terminal)
endif ()

target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(
    ${_target} PROPERTIES
    CXX_STANDARD                  20
    CXX_STANDARD_REQUIRED         YES
    CXX_EXTENSIONS                NO
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")

