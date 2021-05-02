set(source_list
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
    operations/remove_operation.cpp
    operations/remove_operation.hpp
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
    renderers/programs.cpp
    renderers/programs.hpp
    renderers/shadow_renderer.cpp
    renderers/shadow_renderer.hpp
    renderers/text_renderer.cpp
    renderers/text_renderer.hpp

    scene/controller.cpp
    scene/controller.hpp
    scene/frame_controller.cpp
    scene/frame_controller.hpp
    scene/scene_manager.cpp
    scene/scene_manager.hpp

    tools/fly_camera_tool.cpp
    tools/fly_camera_tool.hpp
    tools/grid_tool.cpp
    tools/grid_tool.hpp
    tools/hover_tool.cpp
    tools/hover_tool.hpp
    tools/pointer_context.cpp
    tools/pointer_context.hpp
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
    windows/viewport_config.cpp
    windows/viewport_config.hpp
    windows/viewport_window.cpp
    windows/viewport_window.hpp
    windows/window.cpp
    windows/window.hpp

    application.cpp
    application.hpp
    editor.cpp
    editor.hpp
    erhe_tracy.hpp
    imgui_demo.cpp
    imgui_demo.hpp
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
    erhe::components
    erhe::geometry
    erhe::gl
    erhe::graphics
    erhe::graphics_experimental
    erhe::imgui
    erhe::log
    erhe::primitive
    erhe::scene
    erhe::toolkit
    erhe::ui
    nlohmann_json::nlohmann_json
    TracyClient::TracyClient
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
