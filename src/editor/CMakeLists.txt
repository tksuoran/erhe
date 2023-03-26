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
    graphics/textures.cpp
    graphics/textures.hpp

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
    operations/selection_change_operation.cpp
    operations/selection_change_operation.hpp

    parsers/json_polyhedron.cpp
    parsers/json_polyhedron.hpp
    parsers/wavefront_obj.cpp
    parsers/wavefront_obj.hpp

    renderers/camera_buffer.cpp
    renderers/camera_buffer.hpp
    renderers/draw_indirect_buffer.cpp
    renderers/draw_indirect_buffer.hpp
    renderers/forward_renderer.cpp
    renderers/forward_renderer.hpp
    renderers/frustum_tiler.cpp
    renderers/frustum_tiler.hpp
    renderers/id_renderer.cpp
    renderers/id_renderer.hpp
    renderers/light_buffer.cpp
    renderers/light_buffer.hpp
    renderers/light_mesh.cpp
    renderers/light_mesh.hpp
    renderers/material_buffer.cpp
    renderers/material_buffer.hpp
    renderers/mesh_memory.cpp
    renderers/mesh_memory.hpp
    renderers/pipeline_renderpass.cpp
    renderers/pipeline_renderpass.hpp
    renderers/primitive_buffer.cpp
    renderers/primitive_buffer.hpp
    renderers/program_interface.cpp
    renderers/program_interface.hpp
    renderers/programs.cpp
    renderers/programs.hpp
    renderers/render_context.cpp
    renderers/render_context.hpp
    renderers/shadow_renderer.cpp
    renderers/shadow_renderer.hpp
    renderers/viewport_config.cpp
    renderers/viewport_config.hpp

    rendergraph/basic_viewport_window.cpp
    rendergraph/basic_viewport_window.hpp
    rendergraph/post_processing.cpp
    rendergraph/post_processing.hpp
    rendergraph/shadow_render_node.cpp
    rendergraph/shadow_render_node.hpp

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
    tools/brushes/create/create_box.cpp
    tools/brushes/create/create_box.hpp
    tools/brushes/create/create_cone.cpp
    tools/brushes/create/create_cone.hpp
    tools/brushes/create/create_torus.cpp
    tools/brushes/create/create_torus.hpp
    tools/brushes/create/create_uv_sphere.cpp
    tools/brushes/create/create_uv_sphere.hpp
    tools/trs/handle_visualizations.cpp
    tools/trs/handle_visualizations.hpp
    tools/trs/move_tool.cpp
    tools/trs/move_tool.hpp
    tools/trs/rotate_tool.cpp
    tools/trs/rotate_tool.hpp
    tools/trs/trs_tool.cpp
    tools/trs/trs_tool.hpp
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

    windows/brdf_slice_window.cpp
    windows/brdf_slice_window.hpp
    windows/content_library_window.cpp
    windows/content_library_window.hpp
    windows/debug_view_window.cpp
    windows/debug_view_window.hpp
    windows/imgui_viewport_window.cpp
    windows/imgui_viewport_window.hpp
    windows/layers_window.cpp
    windows/layers_window.hpp
    windows/node_tree_window.cpp
    windows/node_tree_window.hpp
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
    windows/settings.cpp
    windows/settings.hpp
    windows/tool_properties_window.cpp
    windows/tool_properties_window.hpp
    windows/viewport_config_window.cpp
    windows/viewport_config_window.hpp

    editor_application.cpp
    editor_application.hpp
    editor_components.cpp
    editor_log.cpp
    editor_log.hpp
    editor_message.hpp
    editor_message_bus.cpp
    editor_message_bus.hpp
    editor_rendering.cpp
    editor_rendering.hpp
    editor_scenes.cpp
    editor_scenes.hpp
    editor_view_client.cpp
    editor_view_client.hpp
    main.cpp
    rendertarget_mesh.cpp
    rendertarget_mesh.hpp
    rendertarget_imgui_viewport.cpp
    rendertarget_imgui_viewport.hpp
    task_queue.cpp
    task_queue.hpp

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

    res/shaders/brdf_slice.vert
    res/shaders/brdf_slice.frag
    res/shaders/brush.vert
    res/shaders/brush.frag
    res/shaders/anisotropic_slope.vert
    res/shaders/anisotropic_slope.frag
    res/shaders/anisotropic_engine_ready.vert
    res/shaders/anisotropic_engine_ready.frag
    res/shaders/circular_brushed_metal.vert
    res/shaders/circular_brushed_metal.frag
    res/shaders/depth.vert
    res/shaders/depth.frag
    res/shaders/compose.frag
    res/shaders/downsample_x.frag
    res/shaders/downsample_y.frag
    res/shaders/post_processing.vert
    res/shaders/edge_lines.vert
    res/shaders/edge_lines.frag
    res/shaders/id.vert
    res/shaders/id.frag
    res/shaders/line.vert
    res/shaders/line.geom
    res/shaders/line.frag
    res/shaders/points.vert
    res/shaders/points.frag
    res/shaders/standard.vert
    res/shaders/standard.frag
    res/shaders/standard_debug.frag
    res/shaders/sky.vert
    res/shaders/sky.frag
    res/shaders/text.vert
    res/shaders/text.frag
    res/shaders/textured.vert
    res/shaders/textured.frag
    res/shaders/tool.vert
    res/shaders/tool.frag
    res/shaders/visualize_depth.vert
    res/shaders/visualize_depth.frag
    res/shaders/wide_lines.vert
    res/shaders/wide_lines.geom
    res/shaders/wide_lines.frag

    erhe.ini

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
endif ()

target_link_libraries(
    ${_target}
    PRIVATE
    erhe::application
    erhe::components
    erhe::concurrency
    erhe::geometry
    erhe::gl
    erhe::graphics
    erhe::log
    erhe::net
    erhe::physics
    erhe::primitive
    erhe::scene
    erhe::toolkit
    erhe::ui
    mINI
    RectangleBinPack
    rapidjson
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

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
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
    erhe::application
    erhe::log
    erhe::net
    cpp-terminal
    cxxopts
    mINI
    RectangleBinPack
    rapidjson
)

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

