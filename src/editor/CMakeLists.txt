set(_target "editor")

add_executable(${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    graphics/gradients.cpp
    graphics/gradients.hpp
    graphics/icon_set.cpp
    graphics/icon_set.hpp
    graphics/image_transfer.cpp
    graphics/image_transfer.hpp
    graphics/textures.cpp
    graphics/textures.hpp

    operations/attach_detach_operation.cpp
    operations/attach_detach_operation.hpp
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

    renderers/base_renderer.cpp
    renderers/base_renderer.hpp
    renderers/forward_renderer.cpp
    renderers/forward_renderer.hpp
    renderers/frame_resources.cpp
    renderers/frame_resources.hpp
    renderers/id_renderer.cpp
    renderers/id_renderer.hpp
    renderers/light_mesh.cpp
    renderers/light_mesh.hpp
    renderers/mesh_memory.cpp
    renderers/mesh_memory.hpp
    renderers/post_processing.cpp
    renderers/post_processing.hpp
    renderers/program_interface.cpp
    renderers/program_interface.hpp
    renderers/programs.cpp
    renderers/programs.hpp
    renderers/shadow_renderer.cpp
    renderers/shadow_renderer.hpp

    scene/brush.cpp
    scene/brush.hpp
    scene/debug_draw.cpp
    scene/debug_draw.hpp
    scene/frame_controller.cpp
    scene/frame_controller.hpp
    scene/helpers.cpp
    scene/helpers.hpp
    scene/node_physics.cpp
    scene/node_physics.hpp
    scene/node_raytrace.cpp
    scene/node_raytrace.hpp
    scene/scene_builder.cpp
    scene/scene_builder.hpp
    scene/scene_root.cpp
    scene/scene_root.hpp

    tools/fly_camera_tool.cpp
    tools/fly_camera_tool.hpp
    tools/grid_tool.cpp
    tools/grid_tool.hpp
    tools/hover_tool.cpp
    tools/hover_tool.hpp
    tools/material_paint_tool.cpp
    tools/material_paint_tool.hpp
    tools/pointer_context.cpp
    tools/pointer_context.hpp
    tools/physics_tool.cpp
    tools/physics_tool.hpp
    tools/selection_tool.cpp
    tools/selection_tool.hpp
    tools/tool.cpp
    tools/tool.hpp
    tools/tools.cpp
    tools/tools.hpp
    tools/trs_tool.cpp
    tools/trs_tool.hpp

    windows/brushes.cpp
    windows/brushes.hpp
    windows/debug_view_window.cpp
    windows/debug_view_window.hpp
    windows/layers_window.cpp
    windows/layers_window.hpp
    windows/materials.cpp
    windows/materials.hpp
    windows/material_properties.cpp
    windows/material_properties.hpp
    windows/mesh_properties.cpp
    windows/mesh_properties.hpp
    windows/node_properties.cpp
    windows/node_properties.hpp
    windows/node_tree_window.cpp
    windows/node_tree_window.hpp
    windows/operations.cpp
    windows/operations.hpp
    windows/physics_window.cpp
    windows/physics_window.hpp
    windows/tool_properties_window.cpp
    windows/tool_properties_window.hpp
    windows/viewport_config.cpp
    windows/viewport_config.hpp
    windows/viewport_window.cpp
    windows/viewport_window.hpp

    log.cpp
    log.hpp
    main.cpp
    on_load.cpp
    editor_rendering.cpp
    editor_rendering.hpp
    view_client.cpp
    view_client.hpp

    res/icons/camera.svg
    res/icons/directional_light.svg
    res/icons/mesh.svg
    res/icons/node.svg
    res/icons/point_light.svg
    res/icons/spot_light.svg

    res/shaders/brush.vert
    res/shaders/brush.frag
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
)

if (${ERHE_GLTF_LIBRARY} STREQUAL "cgltf")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        parsers/gltf.cpp
        parsers/gltf.hpp
    )
endif ()


if (${ERHE_XR_LIBRARY} STREQUAL "OpenXR")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        xr/controller_visualization.cpp
        xr/controller_visualization.hpp
        xr/hand_tracker.cpp
        xr/hand_tracker.hpp
        xr/headset_renderer.cpp
        xr/headset_renderer.hpp
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
    erhe::physics
    erhe::primitive
    erhe::scene
    erhe::toolkit
    erhe::ui
    imgui
    ghc_filesystem
    optional_lite
    nlohmann_json::nlohmann_json
)

if (${ERHE_SVG_LIBRARY} STREQUAL "lunasvg")
    target_link_libraries(${_target} PRIVATE lunasvg)
endif ()

if (${ERHE_AUDIO_LIBRARY} STREQUAL "miniaudio")
    target_link_libraries(${_target} PRIVATE miniaudio)
endif ()

if (${ERHE_XR_LIBRARY} STREQUAL "OpenXR")
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
