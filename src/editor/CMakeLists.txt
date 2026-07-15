message(STATUS "Configuring executable 'editor'.")

set(_target "editor")
if (ERHE_TARGET_OS_ANDROID)
    # SDL3's Java shim dlopen()s a shared library named "main" (see
    # android.app.lib_name in AndroidManifest.xml). Compile the editor as
    # libmain.so on Android.
    add_library(${_target} SHARED)
    set_target_properties(${_target} PROPERTIES OUTPUT_NAME "main")
    target_link_libraries(${_target} PRIVATE log android)
else ()
    add_executable(${_target})
endif ()

erhe_target_sources_grouped(
    ${_target} TREE "${PROJECT_SOURCE_DIR}" FILES

    config/editor/commands.json
    config/editor/commands_cuboctahedron.json
    config/editor/default_layout.json
    config/editor/desktop_windows.json
    config/editor/editor_settings.json
    config/editor/erhe_graphics.json
    config/editor/graphics_presets.json
    config/editor/logging.json
    config/editor/mesh_memory.json
    config/editor/openxr_commands.json
    config/editor/openxr_windows.json
    config/editor/renderer.json
    config/editor/text_renderer.json

    res/editor/shaders/brdf_slice.frag
    res/editor/shaders/brdf_slice.vert
    res/editor/shaders/brush.frag
    res/editor/shaders/brush.vert
    res/editor/shaders/compose.frag
    res/editor/shaders/depth_to_color.frag
    res/editor/shaders/depth_to_color.vert
    res/editor/shaders/downsample.frag
    res/editor/shaders/downsample_lowpass.frag
    res/editor/shaders/downsample_x.frag
    res/editor/shaders/downsample_y.frag
    res/editor/shaders/post_processing.vert
    res/editor/shaders/shadow_debug.frag
    res/editor/shaders/shadow_debug.vert
    res/editor/shaders/sky.frag
    res/editor/shaders/sky.vert
    res/editor/shaders/grid.frag
    res/editor/shaders/grid.vert
    res/editor/shaders/tool.frag
    res/editor/shaders/tool.vert
    res/editor/shaders/upsample.frag

    res/shaders/compute_before_line.comp
    res/shaders/compute_before_content_line.comp
    res/shaders/edge_lines.frag
    res/shaders/edge_lines.vert
    res/shaders/erhe_bxdf.glsl
    res/shaders/erhe_camera_view.glsl
    res/shaders/erhe_ggx.glsl
    res/shaders/erhe_light.glsl
    res/shaders/erhe_math.glsl
    res/shaders/erhe_srgb.glsl
    res/shaders/erhe_texture.glsl
    res/shaders/erhe_tonemap.glsl
    #res/shaders/error.frag
    #res/shaders/error.vert
    res/shaders/line_after_compute.frag
    res/shaders/line_after_compute.vert
    res/shaders/content_edge_lines.frag
    res/shaders/content_edge_lines.geom
    res/shaders/content_edge_lines.vert
    res/shaders/content_line_after_compute.frag
    res/shaders/debug_line.geom
    res/shaders/debug_line.vert
    res/shaders/standard.frag
    res/shaders/standard.vert
    res/shaders/text.frag
    res/shaders/text.vert
    res/shaders/textured.frag
    res/shaders/textured.vert
)

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    animation/animation_edit.cpp
    animation/animation_edit.hpp
    animation/animation_keying.cpp
    animation/animation_keying.hpp
    animation/animation_player.cpp
    animation/animation_player.hpp
    animation/animation_window.cpp
    animation/animation_window.hpp
    app_context.cpp
    app_context.hpp
    crash_handler.cpp
    crash_handler.hpp
    app_message.hpp
    app_message_bus.cpp
    app_message_bus.hpp
    app_rendering.cpp
    app_rendering.hpp
    app_scenes.cpp
    app_scenes.hpp
    app_settings.cpp
    app_settings.hpp
    app_windows.cpp
    app_windows.hpp
    asset_browser/asset_browser.cpp
    asset_browser/asset_browser.hpp
    brushes/brush.cpp
    brushes/brush.hpp
    brushes/brush_placement.cpp
    brushes/brush_placement.hpp
    brushes/brush_tool.cpp
    brushes/brush_tool.hpp
    brushes/reference_frame.cpp
    brushes/reference_frame.hpp
    CMakeLists.txt
    content_library/brdf_slice.cpp
    content_library/brdf_slice.hpp
    content_library/content_library.cpp
    content_library/content_library.hpp
    content_library/material_library.cpp
    content_library/material_library.hpp
    create/create.cpp
    create/create.hpp
    create/create_box.cpp
    create/create_box.hpp
    create/create_capsule.cpp
    create/create_capsule.hpp
    create/create_cone.cpp
    create/create_cone.hpp
    create/create_preview_settings.hpp
    create/create_shape.cpp
    create/create_shape.hpp
    create/create_torus.cpp
    create/create_torus.hpp
    create/create_uv_sphere.cpp
    create/create_uv_sphere.hpp
    developer/clipboard_window.cpp
    developer/clipboard_window.hpp
    developer/commands_window.cpp
    developer/commands_window.hpp
    developer/composer_window.cpp
    developer/composer_window.hpp
    developer/depth_visualization_window.cpp
    developer/depth_visualization_window.hpp
    developer/icon_browser.cpp
    developer/icon_browser.hpp
    developer/layers_window.cpp
    developer/layers_window.hpp
    developer/post_processing_window.cpp
    developer/post_processing_window.hpp
    developer/rendergraph_window.cpp
    developer/rendergraph_window.hpp
    developer/selection_window.cpp
    developer/selection_window.hpp
    developer/tool_properties_window.cpp
    developer/tool_properties_window.hpp
    editor.cpp
    editor.hpp
    editor_default_layout.cpp
    editor_default_layout.hpp
    editor_log.cpp
    editor_log.hpp
    editor_settings_store.cpp
    editor_settings_store.hpp
    experiments/gradient_editor.cpp
    experiments/gradient_editor.hpp
    experiments/network_window.cpp
    experiments/network_window.hpp
    experiments/sheet_window.cpp
    experiments/sheet_window.hpp
    GenerateIconFontCppHeaders.py
    init_status_display.cpp
    init_status_display.hpp
    geometry_graph/geometry_graph.cpp
    geometry_graph/geometry_graph.hpp
    geometry_graph/geometry_graph_mesh.cpp
    geometry_graph/geometry_graph_mesh.hpp
    geometry_graph/graph_mesh.cpp
    geometry_graph/graph_mesh.hpp
    geometry_graph/graph_mesh_serialization.cpp
    geometry_graph/graph_mesh_serialization.hpp
    geometry_graph/geometry_graph_node.cpp
    geometry_graph/geometry_graph_node.hpp
    geometry_graph/geometry_graph_node_factory.cpp
    geometry_graph/geometry_graph_node_factory.hpp
    geometry_graph/geometry_graph_operations.hpp
    geometry_graph/geometry_graph_window.cpp
    geometry_graph/geometry_graph_window.hpp
    geometry_graph/geometry_payload.cpp
    geometry_graph/geometry_payload.hpp
    geometry_graph/nodes/boolean_node.cpp
    geometry_graph/nodes/boolean_node.hpp
    geometry_graph/nodes/conway_node.cpp
    geometry_graph/nodes/conway_node.hpp
    geometry_graph/nodes/geometry_output_node.cpp
    geometry_graph/nodes/geometry_output_node.hpp
    geometry_graph/nodes/geometry_source_nodes.cpp
    geometry_graph/nodes/geometry_source_nodes.hpp
    geometry_graph/nodes/geometry_unary_operation_node.cpp
    geometry_graph/nodes/geometry_unary_operation_node.hpp
    geometry_graph/nodes/group_nodes.cpp
    geometry_graph/nodes/group_nodes.hpp
    geometry_graph/nodes/instance_nodes.cpp
    geometry_graph/nodes/instance_nodes.hpp
    geometry_graph/nodes/join_geometry_node.cpp
    geometry_graph/nodes/join_geometry_node.hpp
    geometry_graph/nodes/math_node.cpp
    geometry_graph/nodes/math_node.hpp
    geometry_graph/nodes/mesh_box_node.cpp
    geometry_graph/nodes/mesh_box_node.hpp
    geometry_graph/nodes/mesh_cone_node.cpp
    geometry_graph/nodes/mesh_cone_node.hpp
    geometry_graph/nodes/mesh_disc_node.cpp
    geometry_graph/nodes/mesh_disc_node.hpp
    geometry_graph/nodes/mesh_sphere_node.cpp
    geometry_graph/nodes/mesh_sphere_node.hpp
    geometry_graph/nodes/mesh_torus_node.cpp
    geometry_graph/nodes/mesh_torus_node.hpp
    geometry_graph/nodes/subdivide_node.cpp
    geometry_graph/nodes/subdivide_node.hpp
    geometry_graph/nodes/transform_node.cpp
    geometry_graph/nodes/transform_node.hpp
    geometry_graph/nodes/value_nodes.cpp
    geometry_graph/nodes/value_nodes.hpp
    graph/add.cpp
    graph/add.hpp
    graph/constant.cpp
    graph/constant.hpp
    graph/divide.cpp
    graph/divide.hpp
    graph/graph_window.cpp
    graph/graph_window.hpp
    graph/load.cpp
    graph/load.hpp
    graph/multiply.cpp
    graph/multiply.hpp
    graph/node_properties.cpp
    graph/node_properties.hpp
    graph/passthrough.cpp
    graph/passthrough.hpp
    graph/payload.cpp
    graph/payload.hpp
    graph/shader_graph.cpp
    graph/shader_graph.hpp
    graph/shader_graph_node.cpp
    graph/shader_graph_node.hpp
    graph/store.cpp
    graph/store.hpp
    graph/subtract.cpp
    graph/subtract.hpp
    graph_editor/graph_asset.hpp
    graph_editor/graph_editor_node.cpp
    graph_editor/graph_editor_node.hpp
    graph_editor/graph_editor_palette_window.cpp
    graph_editor/graph_editor_palette_window.hpp
    graph_editor/graph_editor_widgets.cpp
    graph_editor/graph_editor_widgets.hpp
    graph_editor/graph_editor_window_base.cpp
    graph_editor/graph_editor_window_base.hpp
    graph_editor/graph_operations.hpp
    graph_editor/graph_serialization.hpp
    graph_editor/node_edge.hpp
    graphics/gradients.cpp
    graphics/gradients.hpp
    graphics/icon_set.cpp
    graphics/icon_set.hpp
    graphics/thumbnails.cpp
    graphics/thumbnails.hpp
    grid/grid.cpp
    grid/grid.hpp
    grid/grid_tool.cpp
    grid/grid_tool.hpp
    input_state.cpp
    input_state.hpp
    items.cpp
    items.hpp
    ImViewGuizmo.cpp
    ImViewGuizmo.h
    main.cpp
    mcp/mcp_server.cpp
    mcp/mcp_server.hpp
    mcp/mcp_server_animation.cpp
    mcp/mcp_server_file_io.cpp
    mcp/mcp_server_graphs.cpp
    mcp/mcp_server_material.cpp
    mcp/mcp_server_mesh_components.cpp
    mcp/mcp_server_physics.cpp
    mcp/mcp_server_scene_action.cpp
    mcp/mcp_server_scene_query.cpp
    mcp/mcp_server_shared.hpp
    mcp/mcp_server_tool_list.cpp
    operations/ambient_light_operation.cpp
    operations/ambient_light_operation.hpp
    operations/animation_edit_operation.cpp
    operations/animation_edit_operation.hpp
    operations/async_raytrace_kickoff_operation.cpp
    operations/async_raytrace_kickoff_operation.hpp
    operations/compound_operation.cpp
    operations/compound_operation.hpp
    operations/content_library_attach_operation.hpp
    operations/geometry_operations.cpp
    operations/geometry_operations.hpp
    operations/operation.cpp
    operations/operation.hpp
    operations/item_insert_remove_operation.cpp
    operations/item_insert_remove_operation.hpp
    operations/item_parent_change_operation.cpp
    operations/item_parent_change_operation.hpp
    operations/item_reposition_in_parent_operation.cpp
    operations/item_reposition_in_parent_operation.hpp
    operations/material_change_operation.cpp
    operations/material_change_operation.hpp
    operations/scene_builder_floor_resources_operation.cpp
    operations/scene_builder_floor_resources_operation.hpp
    operations/scene_builder_viewport_resources_operation.cpp
    operations/scene_builder_viewport_resources_operation.hpp
    operations/merge_operation.cpp
    operations/merge_operation.hpp
    operations/mesh_operation.cpp
    operations/mesh_operation.hpp
    operations/move_mesh_vertices_operation.cpp
    operations/move_mesh_vertices_operation.hpp
    operations/flip_joint_operation.cpp
    operations/flip_joint_operation.hpp
    operations/fork_geometry_operation.cpp
    operations/fork_geometry_operation.hpp
    operations/node_attach_operation.cpp
    operations/node_attach_operation.hpp
    operations/node_transform_operation.cpp
    operations/node_transform_operation.hpp
    operations/operations_window.cpp
    operations/operations_window.hpp
    operations/operation_stack.cpp
    operations/operation_stack.hpp
    operations/scene_open_operation.cpp
    operations/scene_open_operation.hpp
    operations/set_edge_sharpness_operation.cpp
    operations/set_edge_sharpness_operation.hpp
    parsers/geogram.cpp
    parsers/geogram.hpp
    parsers/gltf.cpp
    parsers/gltf.hpp
    parsers/gltf_extensions_export.cpp
    parsers/gltf_extensions_export.hpp
    parsers/gltf_extensions_import.cpp
    parsers/gltf_extensions_import.hpp
    parsers/gltf_extensions_names.hpp
    parsers/gltf_physics_export.cpp
    parsers/gltf_physics_export.hpp
    parsers/gltf_physics_import.cpp
    parsers/gltf_physics_import.hpp
    parsers/json_polyhedron.cpp
    parsers/json_polyhedron.hpp
    parsers/wavefront_obj.cpp
    parsers/wavefront_obj.hpp
    prefabs/prefab_instance.cpp
    prefabs/prefab_instance.hpp
    prefabs/prefab_library.cpp
    prefabs/prefab_library.hpp
    physics/collision_generator.hpp
    physics/physics_tool.cpp
    physics/physics_tool.hpp
    physics/physics_window.cpp
    physics/physics_window.hpp
    preview/brush_preview.cpp
    preview/brush_preview.hpp
    preview/material_preview.cpp
    preview/material_preview.hpp
    preview/scene_preview.cpp
    preview/scene_preview.hpp
    quad_view.cpp
    quad_view.hpp
    renderable.hpp
    renderers/composer.cpp
    renderers/composer.hpp
    renderers/composition_pass.cpp
    renderers/composition_pass.hpp
    renderers/frustum_tiler.cpp
    renderers/frustum_tiler.hpp
    renderers/id_renderer.cpp
    renderers/id_renderer.hpp
    renderers/light_mesh.cpp
    renderers/light_mesh.hpp
    renderers/prewarm.cpp
    renderers/prewarm.hpp
    renderers/programs.cpp
    renderers/programs.hpp
    renderers/render_context.cpp
    renderers/render_context.hpp
    renderers/render_style.cpp
    renderers/render_style.hpp
    renderers/sky_renderer.cpp
    renderers/sky_renderer.hpp
    renderers/viewport_config.cpp
    renderers/viewport_config.hpp
    rendergraph/post_processing.cpp
    rendergraph/post_processing.hpp
    rendergraph/shadow_render_node.cpp
    rendergraph/shadow_render_node.hpp
    rendergraph/viewport_overlay_node.cpp
    rendergraph/viewport_overlay_node.hpp
    mesh_rendertarget_view.cpp
    mesh_rendertarget_view.hpp
    rendertarget_imgui_host.cpp
    rendertarget_imgui_host.hpp
    rendertarget_mesh.cpp
    rendertarget_mesh.hpp
    rendertarget_view.hpp
    robin_hood.h
    scene/attachment_types.cpp
    scene/attachment_types.hpp
    scene/collision_shape_from_mesh.cpp
    scene/collision_shape_from_mesh.hpp
    scene/debug_draw.cpp
    scene/debug_draw.hpp
    scene/frame_controller.cpp
    scene/frame_controller.hpp
    scene/mesh_intersect.cpp
    scene/mesh_intersect.hpp
    scene/node_joint.cpp
    scene/node_joint.hpp
    scene/node_physics.cpp
    scene/node_physics.hpp
    scene/node_raytrace.cpp
    scene/node_raytrace.hpp
    scene/node_raytrace_mask.hpp
    scene/make_mesh_config.hpp
    scene/physics_edits.cpp
    scene/physics_edits.hpp
    scene/scene_builder.cpp
    scene/scene_builder.hpp
    scene/scene_commands.cpp
    scene/scene_commands.hpp
    scene/scene_root.cpp
    scene/scene_root.hpp
    scene/scene_settings_resolve.cpp
    scene/scene_settings_resolve.hpp
    scene/scene_view.cpp
    scene/scene_view.hpp
    scene/shadow_fit_debug.cpp
    scene/shadow_fit_debug.hpp
    scene/viewport_scene_view.cpp
    scene/viewport_scene_view.hpp
    scene/viewport_scene_views.cpp
    scene/viewport_scene_views.hpp
    texture_graph/graph_texture.cpp
    texture_graph/graph_texture.hpp
    texture_graph/graph_texture_serialization.cpp
    texture_graph/graph_texture_serialization.hpp
    texture_graph/texture_graph.cpp
    texture_graph/texture_graph.hpp
    texture_graph/texture_graph_compose.cpp
    texture_graph/texture_graph_compose.hpp
    texture_graph/texture_graph_node.cpp
    texture_graph/texture_graph_node.hpp
    texture_graph/texture_graph_node_factory.cpp
    texture_graph/texture_graph_node_factory.hpp
    texture_graph/texture_graph_operations.hpp
    texture_graph/texture_graph_widgets.cpp
    texture_graph/texture_graph_widgets.hpp
    texture_graph/texture_graph_window.cpp
    texture_graph/texture_graph_window.hpp
    texture_graph/texture_payload.cpp
    texture_graph/texture_payload.hpp
    texture_graph/texture_renderer.cpp
    texture_graph/texture_renderer.hpp
    texture_graph/nodes/texture_buffer_node.cpp
    texture_graph/nodes/texture_buffer_node.hpp
    texture_graph/nodes/texture_descriptor_node.cpp
    texture_graph/nodes/texture_descriptor_node.hpp
    texture_graph/nodes/texture_material_output_node.cpp
    texture_graph/nodes/texture_material_output_node.hpp
    texture_graph/nodes/texture_node_descriptors.cpp
    texture_graph/nodes/texture_node_descriptors.hpp
    texture_graph/nodes/texture_output_node.cpp
    texture_graph/nodes/texture_output_node.hpp
    time.cpp
    time.hpp
    tools/clipboard.cpp
    tools/clipboard.hpp
    tools/debug_visualizations.cpp
    tools/debug_visualizations.hpp
    tools/fly_camera_tool.cpp
    tools/fly_camera_tool.hpp
    tools/hotbar.cpp
    tools/hotbar.hpp
    tools/hover_tool.cpp
    tools/hover_tool.hpp
    tools/hud.cpp
    tools/hud.hpp
    tools/material_paint_tool.cpp
    tools/material_paint_tool.hpp
    tools/mesh_component_selection.cpp
    tools/mesh_component_selection.hpp
    tools/mesh_component_selection_tool.cpp
    tools/mesh_component_selection_tool.hpp
    tools/navigation_gizmo_tool.cpp
    tools/navigation_gizmo_tool.hpp
    tools/paint_tool.cpp
    tools/paint_tool.hpp
    tools/selection_tool.cpp
    tools/selection_tool.hpp
    tools/tool.cpp
    tools/tool.hpp
    tools/tool_window.cpp
    tools/tool_window.hpp
    tools/tools.cpp
    tools/tools.hpp
    transform/handle_enums.cpp
    transform/handle_enums.hpp
    transform/handle_visualizations.cpp
    transform/handle_visualizations.hpp
    transform/mesh_component_extrude.cpp
    transform/mesh_component_extrude.hpp
    transform/mesh_component_transform.cpp
    transform/mesh_component_transform.hpp
    transform/move_tool.cpp
    transform/move_tool.hpp
    transform/rotate_tool.cpp
    transform/rotate_tool.hpp
    transform/rotation_inspector.cpp
    transform/rotation_inspector.hpp
    transform/scale_tool.cpp
    transform/scale_tool.hpp
    transform/subtool.cpp
    transform/subtool.hpp
    transform/transform_tool.cpp
    transform/transform_tool.hpp
    transform/transform_tool_settings.cpp
    transform/transform_tool_settings.hpp
    transform/trs_tool.cpp
    windows/IconsMaterialDesignIcons.h
    windows/active_scene_highlight.cpp
    windows/active_scene_highlight.hpp
    windows/config_ui.cpp
    windows/config_ui.hpp
    windows/editor_windows.cpp
    windows/editor_windows.hpp
    windows/window_placement.cpp
    windows/window_placement.hpp
    windows/inventory_slot_payload.hpp
    windows/inventory_window.cpp
    windows/inventory_window.hpp
    windows/item_reference.cpp
    windows/item_reference.hpp
    windows/item_tree_window.cpp
    windows/item_tree_window.hpp
    windows/properties.cpp
    windows/properties.hpp
    windows/property_editor.cpp
    windows/property_editor.hpp
    windows/scene_view_config_window.cpp
    windows/scene_view_config_window.hpp
    windows/settings_window.cpp
    windows/settings_window.hpp
    windows/viewport_config_window.cpp
    windows/viewport_config_window.hpp
    windows/viewport_window.cpp
    windows/viewport_window.hpp
)

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
        xr/xr_perf_metric_plot.cpp
        xr/xr_perf_metric_plot.hpp
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

if (ERHE_GRAPHICS_API_OPENGL)
    target_link_libraries(${_target} PRIVATE erhe::gl)
endif ()

if (ERHE_USE_MIMALLOC)
    target_link_libraries(${_target} PUBLIC mimalloc-static)
endif ()

# Editor config / library definition dirs (shared by the scene and config codegen
# units below). The scene unit references the editor config structs (Sky_config,
# Grid_config, ...) from Scene_settings, so it must load the same reference-only
# definition dirs the config unit uses (config itself plus graphics/renderer/
# scene_renderer/xr), otherwise validate_references() fails on editor_settings_config.
set(_config_defs         "${CMAKE_CURRENT_SOURCE_DIR}/config/definitions")
set(_gfx_config_defs     "${CMAKE_SOURCE_DIR}/src/erhe/graphics/definitions")
set(_renderer_defs       "${CMAKE_SOURCE_DIR}/src/erhe/renderer/definitions")
set(_scene_renderer_defs "${CMAKE_SOURCE_DIR}/src/erhe/scene_renderer/definitions")
set(_xr_config_defs      "${CMAKE_SOURCE_DIR}/src/erhe/xr/definitions")

# Scene codegen — generate into build directory. Gltf_source_reference is
# used by the content library; Scene_settings carries per-scene setting
# overrides (#239) persisted in the ERHE_scene glTF extension. (The legacy
# .erhescene scene.json serial types were removed with scene_serialization
# in the glTF scene roundtrip phase 5.)
set(_codegen_defs "${CMAKE_CURRENT_SOURCE_DIR}/scene/definitions")
set(_codegen_out  "${CMAKE_CURRENT_BINARY_DIR}/scene/generated")

erhe_codegen_generate(
    TARGET          ${_target}
    DEFINITIONS_DIR "${_codegen_defs}"
    OUTPUT_DIR      "${_codegen_out}"
    EXTRA_DEFINITIONS_DIRS
        "${_config_defs}:config/generated/"
        "${_gfx_config_defs}:erhe_graphics/generated/"
        "${_renderer_defs}:erhe_renderer/generated/"
        "${_scene_renderer_defs}:erhe_scene_renderer/generated/"
        "${_xr_config_defs}:erhe_xr/generated/"
    DEFINITIONS
        "${_codegen_defs}/gltf_source_reference.py"
        "${_codegen_defs}/scene_settings.py"
)

set(_codegen_sources
    ${_codegen_out}/gltf_source_reference.hpp
    ${_codegen_out}/gltf_source_reference_serialization.hpp
    ${_codegen_out}/gltf_source_reference.cpp
    ${_codegen_out}/scene_settings.hpp
    ${_codegen_out}/scene_settings_serialization.hpp
    ${_codegen_out}/scene_settings.cpp
)
set_source_files_properties(${_codegen_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_codegen_sources})
source_group("codegen generated/scene" FILES ${_codegen_sources})

# Editor config codegen — generate into build directory
# (_config_defs / _gfx_config_defs / _renderer_defs / _scene_renderer_defs /
#  _xr_config_defs are defined above, before the scene codegen unit.)
set(_config_out  "${CMAKE_CURRENT_BINARY_DIR}/config/generated")

erhe_codegen_generate(
    TARGET          ${_target}
    DEFINITIONS_DIR "${_config_defs}"
    OUTPUT_DIR      "${_config_out}"
    EXTRA_DEFINITIONS_DIRS
        "${_gfx_config_defs}:erhe_graphics/generated/"
        "${_renderer_defs}:erhe_renderer/generated/"
        "${_scene_renderer_defs}:erhe_scene_renderer/generated/"
        "${_xr_config_defs}:erhe_xr/generated/"
    DEFINITIONS
        "${_config_defs}/camera_controls_config.py"
        "${_config_defs}/developer_config.py"
        "${_config_defs}/geometry_edit_mode.py"
        "${_config_defs}/mesh_transform_mode.py"
        "${_config_defs}/editor_settings_config.py"
        "${_config_defs}/grid_config.py"
        "${_config_defs}/hotbar_config.py"
        "${_config_defs}/inventory_config.py"
        "${_config_defs}/hud_config.py"
        "${_config_defs}/id_renderer_config.py"
        "${_config_defs}/network_config.py"
        "${_config_defs}/physics_config.py"
        "${_config_defs}/renderer_config.py"
        "${_config_defs}/scene_config.py"
        "${_config_defs}/shadow_frustum_fit_config.py"
        "${_config_defs}/sky_config.py"
        "${_config_defs}/text_renderer_config.py"
        "${_config_defs}/threading_config.py"
        "${_config_defs}/thumbnails_config.py"
        "${_config_defs}/transform_tool_config.py"
        "${_config_defs}/viewport_config_data.py"
        "${_config_defs}/window_config.py"
        "${_config_defs}/imgui_settings_config.py"
        "${_config_defs}/icon_settings_config.py"
        "${_config_defs}/shadow_filter_mode.py"
        "${_config_defs}/shadow_bias_mode.py"
        "${_config_defs}/shadow_cull_mode.py"
        "${_config_defs}/shadow_technique_mode.py"
        "${_config_defs}/hotbar_anchor.py"
        "${_config_defs}/graphics_preset_entry.py"
        "${_config_defs}/graphics_presets_config.py"
        "${_config_defs}/fly_camera_config.py"
        "${_config_defs}/render_style_data.py"
        "${_config_defs}/render_style_appearance.py"
        "${_config_defs}/selection_outline_style.py"
        "${_config_defs}/content_edge_lines_config.py"
        "${_config_defs}/preview_edge_lines_config.py"
        "${_config_defs}/mesh_component_style.py"
        "${_config_defs}/debug_visualizations_config.py"
        "${_config_defs}/debug_visualizations_settings.py"
        "${_config_defs}/debug_visualizations_style.py"
        "${_config_defs}/scene_and_camera_settings.py"
        "${_config_defs}/scene_view_settings.py"
        "${_config_defs}/default_viewport_config.py"
        "${_config_defs}/dock_direction.py"
        "${_config_defs}/dock_placement.py"
        "${_config_defs}/default_layout_config.py"
        "${_config_defs}/make_mesh_args.py"
        "${_config_defs}/add_lights_args.py"
        "${_config_defs}/add_room_args.py"
        "${_config_defs}/add_cameras_args.py"
)

set(_config_sources
    ${_config_out}/camera_controls_config.hpp
    ${_config_out}/camera_controls_config_serialization.hpp
    ${_config_out}/camera_controls_config.cpp
    ${_config_out}/developer_config.hpp
    ${_config_out}/developer_config_serialization.hpp
    ${_config_out}/developer_config.cpp
    ${_config_out}/grid_config.hpp
    ${_config_out}/grid_config_serialization.hpp
    ${_config_out}/grid_config.cpp
    ${_config_out}/hotbar_anchor.hpp
    ${_config_out}/hotbar_anchor.cpp
    ${_config_out}/hotbar_config.hpp
    ${_config_out}/hotbar_config_serialization.hpp
    ${_config_out}/hotbar_config.cpp
    ${_config_out}/hud_config.hpp
    ${_config_out}/hud_config_serialization.hpp
    ${_config_out}/hud_config.cpp
    ${_config_out}/operation_params.hpp
    ${_config_out}/operation_params_serialization.hpp
    ${_config_out}/operation_params.cpp
    ${_config_out}/inventory_slot.hpp
    ${_config_out}/inventory_slot_serialization.hpp
    ${_config_out}/inventory_slot.cpp
    ${_config_out}/inventory_config.hpp
    ${_config_out}/inventory_config_serialization.hpp
    ${_config_out}/inventory_config.cpp
    ${_config_out}/id_renderer_config.hpp
    ${_config_out}/id_renderer_config_serialization.hpp
    ${_config_out}/id_renderer_config.cpp
    ${_config_out}/network_config.hpp
    ${_config_out}/network_config_serialization.hpp
    ${_config_out}/network_config.cpp
    ${_config_out}/physics_config.hpp
    ${_config_out}/physics_config_serialization.hpp
    ${_config_out}/physics_config.cpp
    ${_config_out}/renderer_config.hpp
    ${_config_out}/renderer_config_serialization.hpp
    ${_config_out}/renderer_config.cpp
    ${_config_out}/scene_config.hpp
    ${_config_out}/scene_config_serialization.hpp
    ${_config_out}/scene_config.cpp
    ${_config_out}/shadow_frustum_fit_config.hpp
    ${_config_out}/shadow_frustum_fit_config_serialization.hpp
    ${_config_out}/shadow_frustum_fit_config.cpp
    ${_config_out}/sky_config.hpp
    ${_config_out}/sky_config_serialization.hpp
    ${_config_out}/sky_config.cpp
    ${_config_out}/text_renderer_config.hpp
    ${_config_out}/text_renderer_config_serialization.hpp
    ${_config_out}/text_renderer_config.cpp
    ${_config_out}/threading_config.hpp
    ${_config_out}/threading_config_serialization.hpp
    ${_config_out}/threading_config.cpp
    ${_config_out}/thumbnails_config.hpp
    ${_config_out}/thumbnails_config_serialization.hpp
    ${_config_out}/thumbnails_config.cpp
    ${_config_out}/transform_tool_config.hpp
    ${_config_out}/transform_tool_config_serialization.hpp
    ${_config_out}/transform_tool_config.cpp
    ${_config_out}/viewport_config_data.hpp
    ${_config_out}/viewport_config_data_serialization.hpp
    ${_config_out}/viewport_config_data.cpp
    ${_config_out}/window_config.hpp
    ${_config_out}/window_config_serialization.hpp
    ${_config_out}/window_config.cpp
    ${_config_out}/imgui_settings_config.hpp
    ${_config_out}/imgui_settings_config_serialization.hpp
    ${_config_out}/imgui_settings_config.cpp
    ${_config_out}/icon_settings_config.hpp
    ${_config_out}/icon_settings_config_serialization.hpp
    ${_config_out}/icon_settings_config.cpp
    ${_config_out}/shadow_filter_mode.hpp
    ${_config_out}/shadow_filter_mode.cpp
    ${_config_out}/shadow_bias_mode.hpp
    ${_config_out}/shadow_bias_mode.cpp
    ${_config_out}/shadow_cull_mode.hpp
    ${_config_out}/shadow_cull_mode.cpp
    ${_config_out}/shadow_technique_mode.hpp
    ${_config_out}/shadow_technique_mode.cpp
    ${_config_out}/graphics_preset_entry.hpp
    ${_config_out}/graphics_preset_entry_serialization.hpp
    ${_config_out}/graphics_preset_entry.cpp
    ${_config_out}/graphics_presets_config.hpp
    ${_config_out}/graphics_presets_config_serialization.hpp
    ${_config_out}/graphics_presets_config.cpp
    ${_config_out}/fly_camera_config.hpp
    ${_config_out}/fly_camera_config_serialization.hpp
    ${_config_out}/fly_camera_config.cpp
    ${_config_out}/geometry_edit_mode.hpp
    ${_config_out}/geometry_edit_mode.cpp
    ${_config_out}/mesh_transform_mode.hpp
    ${_config_out}/mesh_transform_mode.cpp
    ${_config_out}/editor_settings_config.hpp
    ${_config_out}/editor_settings_config_serialization.hpp
    ${_config_out}/editor_settings_config.cpp
    ${_config_out}/render_style_data.hpp
    ${_config_out}/render_style_data_serialization.hpp
    ${_config_out}/render_style_data.cpp
    ${_config_out}/render_style_appearance.hpp
    ${_config_out}/render_style_appearance_serialization.hpp
    ${_config_out}/render_style_appearance.cpp
    ${_config_out}/selection_outline_style.hpp
    ${_config_out}/selection_outline_style_serialization.hpp
    ${_config_out}/selection_outline_style.cpp
    ${_config_out}/content_edge_lines_config.hpp
    ${_config_out}/content_edge_lines_config_serialization.hpp
    ${_config_out}/content_edge_lines_config.cpp
    ${_config_out}/preview_edge_lines_config.hpp
    ${_config_out}/preview_edge_lines_config_serialization.hpp
    ${_config_out}/preview_edge_lines_config.cpp
    ${_config_out}/mesh_component_style.hpp
    ${_config_out}/mesh_component_style_serialization.hpp
    ${_config_out}/mesh_component_style.cpp
    ${_config_out}/debug_visualizations_config.hpp
    ${_config_out}/debug_visualizations_config_serialization.hpp
    ${_config_out}/debug_visualizations_config.cpp
    ${_config_out}/debug_visualizations_settings.hpp
    ${_config_out}/debug_visualizations_settings_serialization.hpp
    ${_config_out}/debug_visualizations_settings.cpp
    ${_config_out}/debug_visualizations_style.hpp
    ${_config_out}/debug_visualizations_style_serialization.hpp
    ${_config_out}/debug_visualizations_style.cpp
    ${_config_out}/scene_and_camera_settings.hpp
    ${_config_out}/scene_and_camera_settings_serialization.hpp
    ${_config_out}/scene_and_camera_settings.cpp
    ${_config_out}/scene_view_settings.hpp
    ${_config_out}/scene_view_settings_serialization.hpp
    ${_config_out}/scene_view_settings.cpp
    ${_config_out}/dock_direction.hpp
    ${_config_out}/dock_direction.cpp
    ${_config_out}/dock_placement.hpp
    ${_config_out}/dock_placement_serialization.hpp
    ${_config_out}/dock_placement.cpp
    ${_config_out}/default_layout_config.hpp
    ${_config_out}/default_layout_config_serialization.hpp
    ${_config_out}/default_layout_config.cpp
    ${_config_out}/make_mesh_args.hpp
    ${_config_out}/make_mesh_args_serialization.hpp
    ${_config_out}/make_mesh_args.cpp
    ${_config_out}/add_lights_args.hpp
    ${_config_out}/add_lights_args_serialization.hpp
    ${_config_out}/add_lights_args.cpp
    ${_config_out}/add_room_args.hpp
    ${_config_out}/add_room_args_serialization.hpp
    ${_config_out}/add_room_args.cpp
    ${_config_out}/add_cameras_args.hpp
    ${_config_out}/add_cameras_args_serialization.hpp
    ${_config_out}/add_cameras_args.cpp
    ${_config_out}/viewport_config.hpp
    ${_config_out}/viewport_config_serialization.hpp
    ${_config_out}/viewport_config.cpp
)
set_source_files_properties(${_config_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_config_sources})
source_group("codegen generated/config" FILES ${_config_sources})

# Serialize/deserialize .cpp from library codegen must compile here (same
# target as other simdjson users) to avoid ABI mismatch from differing arch flags.
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

set(_renderer_gen "${CMAKE_BINARY_DIR}/src/erhe/renderer/erhe_renderer/generated")
set(_renderer_ser_sources
    ${_renderer_gen}/visualization_mode.hpp
    ${_renderer_gen}/visualization_mode.cpp
)
set_source_files_properties(${_renderer_ser_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_renderer_ser_sources})
source_group("codegen generated/renderer" FILES ${_renderer_ser_sources})

set(_scene_renderer_gen "${CMAKE_BINARY_DIR}/src/erhe/scene_renderer/erhe_scene_renderer/generated")
set(_scene_renderer_ser_sources
    ${_scene_renderer_gen}/primitive_color_source.hpp
    ${_scene_renderer_gen}/primitive_color_source.cpp
    ${_scene_renderer_gen}/mesh_memory_config_serialization.hpp
    ${_scene_renderer_gen}/mesh_memory_config.cpp
)
set_source_files_properties(${_scene_renderer_ser_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_scene_renderer_ser_sources})
source_group("codegen generated/scene_renderer" FILES ${_scene_renderer_ser_sources})

set(_xr_gen "${CMAKE_BINARY_DIR}/src/erhe/xr/erhe_xr/generated")
set(_xr_ser_sources
    ${_xr_gen}/headset_config_serialization.hpp
    ${_xr_gen}/headset_config.cpp
    ${_xr_gen}/perf_settings_level.cpp
    ${_xr_gen}/foveation_level.cpp
)
set_source_files_properties(${_xr_ser_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_xr_ser_sources})
source_group("codegen generated/xr" FILES ${_xr_ser_sources})

target_link_libraries(
    ${_target}
    PRIVATE
        erhe::codegen
        erhe::utility
        erhe::commands
        httplib::httplib
        nlohmann_json::nlohmann_json
        cxxopts
        erhe::defer
        erhe::dataformat
        erhe::file
        erhe::geometry
        erhe::geometry_renderer
        erhe::gltf
        erhe::graph
        erhe::graphics
        erhe::imgui
        erhe::log
        erhe::math
        erhe::message_bus
        erhe::net
        erhe::physics
        erhe::primitive
        erhe::profile
        erhe::renderer
        erhe::rendergraph
        erhe::scene
        erhe::scene_renderer
        erhe::texgen
        erhe::time
        erhe::ui
        erhe::verify
        #meshoptimizer
        geogram
        imgui
        imgui_gradient::imgui_gradient
        rapidjson
        RectangleBinPack
        # reflectcpp::reflectcpp
        Taskflow
        tinyexpr
)

# Icon font download and process
find_package(Python3 COMPONENTS Interpreter)
if (NOT ${Python3_FOUND})
    message(FATAL_ERROR "Python3 is needed to generate sources.")
endif ()
set(ICON_FONT_MDI_DATA_URI  "https://github.com/Templarian/MaterialDesign-Webfont/raw/master/css/materialdesignicons.css")
set(ICON_FONT_MDI_DATA_FILE "${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/materialdesignicons.css")
set(ICON_FONT_MDI_TTF_URI   "https://github.com/Templarian/MaterialDesign-Webfont/raw/master/fonts/materialdesignicons-webfont.ttf")
set(ICON_FONT_MDI_TTF_FILE  "${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/materialdesignicons-webfont.ttf")
if (NOT EXISTS ${ICON_FONT_MDI_DATA_FILE})
    message(STATUS "Downloading Material Design Icons font .css...")
    file(DOWNLOAD "${ICON_FONT_MDI_DATA_URI}" "${ICON_FONT_MDI_DATA_FILE}" SHOW_PROGRESS)
endif()
if (NOT EXISTS ${ICON_FONT_MDI_TTF_FILE})
    message(STATUS "Downloading Material Design Icons font .ttf...")
    file(DOWNLOAD "${ICON_FONT_MDI_TTF_URI}" "${ICON_FONT_MDI_TTF_FILE}" SHOW_PROGRESS)
endif()
message(STATUS "Running GenerateIconFontCppHeaders.py to generate MaterialDesignIcon font header file")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/GenerateIconFontCppHeaders.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
    target_link_libraries(${_target} PRIVATE imgui)
endif ()
if (${ERHE_SVG_LIBRARY} STREQUAL "plutosvg")
    target_link_libraries(${_target} PRIVATE plutosvg)
endif ()
if (${ERHE_AUDIO_LIBRARY} STREQUAL "miniaudio")
    target_link_libraries(${_target} PRIVATE miniaudio)
endif ()
if (${ERHE_NAVIGATION_LIBRARY} STREQUAL "recastnavigation")
    target_link_libraries(
        ${_target}
        PRIVATE
            RecastNavigation::Recast
            RecastNavigation::Detour
            RecastNavigation::DetourCrowd
            RecastNavigation::DetourTileCache
            RecastNavigation::DebugUtils
    )
endif ()
# erhe::xr is always linked because it owns Headset_config (used by editor
# settings serialization regardless of XR backend). The OpenXR loader is only
# linked when the OpenXR backend is enabled.
target_link_libraries(${_target} PRIVATE erhe::xr)
if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    target_link_libraries(${_target} PRIVATE openxr_loader)
endif ()
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${_codegen_out})

# dbghelp provides MiniDumpWriteDump for the crash handler (crash_handler.cpp).
if (WIN32)
    target_link_libraries(${_target} PRIVATE dbghelp)
endif ()

set_target_properties(
    ${_target} PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS        NO
)

erhe_target_settings(${_target} "erhe-executables")

########

if (NOT ERHE_TARGET_OS_ANDROID)
    set(_target "net-test")
    add_executable(${_target})
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        net_test.cpp
    )

    if (ERHE_USE_MIMALLOC)
        target_link_libraries(${_target} PUBLIC mimalloc-static)
    endif ()

    target_link_libraries(
        ${_target}
        PRIVATE
            geogram
            erhe::log
            erhe::net
            cxxopts
            RectangleBinPack
            rapidjson
    )
    if (${ERHE_TERMINAL_LIBRARY} STREQUAL "cpp-terminal")
        target_link_libraries(${_target} PRIVATE cpp-terminal)
    endif ()

    target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    # MSVC only target_link_options(${_target} PRIVATE "/MAP:${CMAKE_CURRENT_SOURCE_DIR}/editor.map")

    erhe_target_settings(${_target} "erhe-executables")
endif ()

if (${ERHE_BUILD_TESTS})
    add_subdirectory(mcp/test)
endif ()
