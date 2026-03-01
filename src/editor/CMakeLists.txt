message(STATUS "Configuring executable 'editor'.")

set(_target "editor")
add_executable(${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe.toml
    graphics_presets.toml
    logging.toml
    openxr_windows.toml
    settings.toml
    windows.toml

    animation/animation_curve.cpp
    animation/animation_curve.hpp
    animation/animation_window.cpp
    animation/animation_window.hpp
    animation/timeline_window.cpp
    animation/timeline_window.hpp
    app_context.cpp
    app_context.hpp
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
    editor_log.cpp
    editor_log.hpp
    experiments/gradient_editor.cpp
    experiments/gradient_editor.hpp
    experiments/network_window.cpp
    experiments/network_window.hpp
    experiments/sheet_window.cpp
    experiments/sheet_window.hpp
    GenerateIconFontCppHeaders.py
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
    operations/compound_operation.cpp
    operations/compound_operation.hpp
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
    operations/merge_operation.cpp
    operations/merge_operation.hpp
    operations/mesh_operation.cpp
    operations/mesh_operation.hpp
    operations/node_attach_operation.cpp
    operations/node_attach_operation.hpp
    operations/node_transform_operation.cpp
    operations/node_transform_operation.hpp
    operations/operations_window.cpp
    operations/operations_window.hpp
    operations/operation_stack.cpp
    operations/operation_stack.hpp
    parsers/geogram.cpp
    parsers/geogram.hpp
    parsers/gltf.cpp
    parsers/gltf.hpp
    parsers/json_polyhedron.cpp
    parsers/json_polyhedron.hpp
    parsers/wavefront_obj.cpp
    parsers/wavefront_obj.hpp
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
    renderers/mesh_memory.cpp
    renderers/mesh_memory.hpp
    renderers/programs.cpp
    renderers/programs.hpp
    renderers/render_context.cpp
    renderers/render_context.hpp
    renderers/render_style.cpp
    renderers/render_style.hpp
    renderers/viewport_config.cpp
    renderers/viewport_config.hpp
    rendergraph/basic_scene_view_node.cpp
    rendergraph/basic_scene_view_node.hpp
    rendergraph/post_processing.cpp
    rendergraph/post_processing.hpp
    rendergraph/shadow_render_node.cpp
    rendergraph/shadow_render_node.hpp
    rendertarget_imgui_host.cpp
    rendertarget_imgui_host.hpp
    rendertarget_mesh.cpp
    rendertarget_mesh.hpp
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
    res/shaders/depth_only.frag
    res/shaders/depth_only.vert
    res/shaders/downsample.frag
    res/shaders/downsample_lowpass.frag
    res/shaders/downsample_x.frag
    res/shaders/downsample_y.frag
    res/shaders/edge_lines.frag
    res/shaders/edge_lines.vert
    res/shaders/erhe_bxdf.glsl
    res/shaders/erhe_ggx.glsl
    res/shaders/erhe_light.glsl
    res/shaders/erhe_math.glsl
    res/shaders/erhe_srgb.glsl
    res/shaders/erhe_texture.glsl
    res/shaders/erhe_tonemap.glsl
    res/shaders/error.frag
    res/shaders/error.vert
    res/shaders/fat_triangle.frag
    res/shaders/fat_triangle.geom
    res/shaders/fat_triangle.vert
    res/shaders/grid.frag
    res/shaders/grid.vert
    res/shaders/id.frag
    res/shaders/id.vert
    res/shaders/line_after_compute.frag
    res/shaders/line_after_compute.vert
    res/shaders/points.frag
    res/shaders/points.vert
    res/shaders/post_processing.vert
    res/shaders/shadow_debug.frag
    res/shaders/shadow_debug.vert
    res/shaders/sky.frag
    res/shaders/sky.vert
    res/shaders/standard.frag
    res/shaders/standard.vert
    res/shaders/standard_debug.frag
    res/shaders/standard_debug.vert
    res/shaders/text.frag
    res/shaders/text.vert
    res/shaders/textured.frag
    res/shaders/textured.vert
    res/shaders/tool.frag
    res/shaders/tool.vert
    res/shaders/upsample.frag
    res/shaders/visualize_depth.frag
    res/shaders/visualize_depth.vert
    res/shaders/wide_lines.frag
    res/shaders/wide_lines.geom
    res/shaders/wide_lines.vert
    robin_hood.h
    scene/debug_draw.cpp
    scene/debug_draw.hpp
    scene/frame_controller.cpp
    scene/frame_controller.hpp
    scene/mesh_intersect.cpp
    scene/mesh_intersect.hpp
    scene/node_physics.cpp
    scene/node_physics.hpp
    scene/node_raytrace.cpp
    scene/node_raytrace.hpp
    scene/node_raytrace_mask.hpp
    scene/scene_builder.cpp
    scene/scene_builder.hpp
    scene/scene_commands.cpp
    scene/scene_commands.hpp
    scene/scene_root.cpp
    scene/scene_root.hpp
    scene/scene_view.cpp
    scene/scene_view.hpp
    scene/viewport_scene_view.cpp
    scene/viewport_scene_view.hpp
    scene/viewport_scene_views.cpp
    scene/viewport_scene_views.hpp
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
    tools/paint_tool.cpp
    tools/paint_tool.hpp
    tools/selection_tool.cpp
    tools/selection_tool.hpp
    tools/tool.cpp
    tools/tool.hpp
    tools/tools.cpp
    tools/tools.hpp
    transform/handle_enums.cpp
    transform/handle_enums.hpp
    transform/handle_visualizations.cpp
    transform/handle_visualizations.hpp
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

target_link_libraries(
    ${_target}
    PRIVATE
    erhe::utility
    erhe::commands
    erhe::configuration
    erhe::defer
    erhe::dataformat
    erhe::file
    erhe::geometry
    erhe::geometry_renderer
    erhe::gltf
    erhe::graph
    erhe::graphics
    erhe::graphics_buffer_sink
    erhe::imgui
    erhe::log
    erhe::math
    erhe::net
    erhe::physics
    erhe::primitive
    erhe::profile
    erhe::renderer
    erhe::rendergraph
    erhe::scene
    erhe::scene_renderer
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
    tomlplusplus::tomlplusplus
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
if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    target_link_libraries(${_target} PRIVATE erhe_xr openxr_loader)
endif ()
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(
    ${_target} PROPERTIES
    CXX_STANDARD                  20
    CXX_STANDARD_REQUIRED         YES
    CXX_EXTENSIONS                NO
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEBUGGER_WORKING_DIRECTORY    "${CMAKE_CURRENT_SOURCE_DIR}"
)

erhe_target_settings(${_target} "erhe-executables")

########

set(_target "net-test")
add_executable(${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    net_test.cpp
)
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
