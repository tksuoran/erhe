set(source_list
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
    renderers/line_renderer.cpp
    renderers/line_renderer.hpp
    renderers/mesh_memory.cpp
    renderers/mesh_memory.hpp
    renderers/program_interface.cpp
    renderers/program_interface.hpp
    renderers/programs.cpp
    renderers/programs.hpp
    renderers/shadow_renderer.cpp
    renderers/shadow_renderer.hpp
    renderers/text_renderer.cpp
    renderers/text_renderer.hpp

    scene/brush.cpp
    scene/brush.hpp
    scene/controller.cpp
    scene/controller.hpp
    scene/debug_draw.cpp
    scene/debug_draw.hpp
    scene/frame_controller.cpp
    scene/frame_controller.hpp
    scene/helpers.cpp
    scene/helpers.hpp
    scene/node_physics.cpp
    scene/node_physics.hpp
    scene/scene_manager.cpp
    scene/scene_manager.hpp
    scene/scene_root.cpp
    scene/scene_root.hpp

    tools/fly_camera_tool.cpp
    tools/fly_camera_tool.hpp
    tools/grid_tool.cpp
    tools/grid_tool.hpp
    tools/hover_tool.cpp
    tools/hover_tool.hpp
    tools/pointer_context.cpp
    tools/pointer_context.hpp
    tools/physics_tool.cpp
    tools/physics_tool.hpp
    tools/selection_tool.cpp
    tools/selection_tool.hpp
    tools/trs_tool.cpp
    tools/trs_tool.hpp
    tools/tool.cpp
    tools/tool.hpp

    windows/brushes.cpp
    windows/brushes.hpp
    windows/camera_properties.cpp
    windows/camera_properties.hpp
    windows/light_properties.cpp
    windows/light_properties.hpp
    windows/material_properties.cpp
    windows/material_properties.hpp
    windows/mesh_properties.cpp
    windows/mesh_properties.hpp
    windows/node_properties.cpp
    windows/node_properties.hpp
    windows/operations.cpp
    windows/operations.hpp
    windows/physics_window.cpp
    windows/physics_window.hpp
    windows/viewport_config.cpp
    windows/viewport_config.hpp
    windows/viewport_window.cpp
    windows/viewport_window.hpp
    windows/window.cpp
    windows/window.hpp

    application.cpp
    application.hpp
    concurrentqueue.h
    rendering.cpp
    rendering.hpp
    time.cpp
    time.hpp
    tools.cpp
    tools.hpp
    view.cpp
    view.hpp
    editor.hpp
    gl_context_provider.cpp
    gl_context_provider.hpp
    log.cpp
    log.hpp
    main.cpp
    on_load.cpp
    textures.cpp
    textures.hpp
    TracyClient.cpp

    res/shaders/anisotropic.vert
    res/shaders/anisotropic.frag
    res/shaders/basic.vert
    res/shaders/basic.frag
    res/shaders/color.vert
    res/shaders/color.frag
    res/shaders/depth.vert
    res/shaders/depth.frag
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
    res/shaders/wide_lines.vert
    res/shaders/wide_lines.geom
    res/shaders/wide_lines.frag
)

add_executable(editor ${source_list})

target_link_libraries(
    editor
    PRIVATE
    erhe_components
    erhe_geometry
    erhe_gl
    erhe_graphics
    erhe_graphics_experimental
    erhe_imgui
    erhe_log
    erhe_physics
    erhe_primitive
    erhe_scene
    erhe_toolkit
    erhe_ui
    erhe_xr
    mango
    nlohmann_json::nlohmann_json
    openxr_loader
    TracyClient
)

target_include_directories(editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(editor PROPERTIES
                      CXX_STANDARD                  20
                      CXX_STANDARD_REQUIRED         YES
                      CXX_EXTENSIONS                NO
                      VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
