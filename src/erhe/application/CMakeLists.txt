set(_target erhe_application)
add_library(${_target})
add_library(erhe::application ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    commands/command.cpp
    commands/command.hpp
    commands/commands.cpp
    commands/commands.hpp
    commands/command_binding.cpp
    commands/command_binding.hpp
    commands/command_context.cpp
    commands/command_context.hpp
    commands/key_binding.cpp
    commands/key_binding.hpp
    commands/mouse_binding.cpp
    commands/mouse_binding.hpp
    commands/mouse_button_binding.cpp
    commands/mouse_button_binding.hpp
    commands/mouse_motion_binding.cpp
    commands/mouse_motion_binding.hpp
    commands/mouse_drag_binding.cpp
    commands/mouse_drag_binding.hpp
    commands/mouse_wheel_binding.cpp
    commands/mouse_wheel_binding.hpp
    commands/state.cpp
    commands/state.hpp
    commands/update_binding.cpp
    commands/update_binding.hpp

    graphics/gl_context_provider.cpp
    graphics/gl_context_provider.hpp
    graphics/shader_monitor.cpp
    graphics/shader_monitor.hpp

    renderers/buffer_writer.cpp
    renderers/buffer_writer.hpp
    renderers/line_renderer.cpp
    renderers/line_renderer.hpp
    renderers/multi_buffer.cpp
    renderers/multi_buffer.hpp
    renderers/text_renderer.cpp
    renderers/text_renderer.hpp

    rendergraph/multisample_resolve.cpp
    rendergraph/multisample_resolve.hpp
    rendergraph/rendergraph.cpp
    rendergraph/rendergraph.hpp
    rendergraph/rendergraph_node.cpp
    rendergraph/rendergraph_node.hpp
    rendergraph/resource_routing.cpp
    rendergraph/resource_routing.hpp
    rendergraph/sink_rendergraph_node.cpp
    rendergraph/sink_rendergraph_node.hpp
    rendergraph/texture_rendergraph_node.cpp
    rendergraph/texture_rendergraph_node.hpp

    windows/commands_window.cpp
    windows/commands_window.hpp
    windows/framebuffer_window.cpp
    windows/framebuffer_window.hpp
    windows/log_window.cpp
    windows/log_window.hpp
    windows/performance_window.cpp
    windows/performance_window.hpp
    windows/pipelines.cpp
    windows/pipelines.hpp

    application_log.cpp
    application_log.hpp
    configuration.cpp
    configuration.hpp
    controller.cpp
    controller.hpp
    renderdoc_capture_support.cpp
    renderdoc_capture_support.hpp
    time.cpp
    time.hpp
    application_view.cpp
    application_view.hpp
    window.cpp
    window.hpp
)

if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        imgui/imgui_helpers.cpp
        imgui/imgui_helpers.hpp
        imgui/imgui_renderer.cpp
        imgui/imgui_renderer.hpp
        imgui/imgui_viewport.cpp
        imgui/imgui_viewport.hpp
        imgui/imgui_window.cpp
        imgui/imgui_window.hpp
        imgui/imgui_windows.cpp
        imgui/imgui_windows.hpp
        imgui/scoped_imgui_context.cpp
        imgui/scoped_imgui_context.hpp
        imgui/window_imgui_viewport.cpp
        imgui/window_imgui_viewport.hpp
        imgui/ImNodes.cpp
        imgui/ImNodes.h
        imgui/ImNodesEz.cpp
        imgui/ImNodesEz.h
    )
    target_link_libraries(${_target} PUBLIC imgui)
endif ()

if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        commands/xr_boolean_binding.cpp
        commands/xr_boolean_binding.hpp
        commands/xr_float_binding.cpp
        commands/xr_float_binding.hpp
        commands/xr_vector2f_binding.cpp
        commands/xr_vector2f_binding.hpp
    )
    target_link_libraries(${_target} PUBLIC erhe::xr)
endif ()


target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        cxxopts
        etl::etl
        fmt::fmt
        erhe::concurrency
        erhe::gl
        erhe::graphics
        erhe::log
        erhe::toolkit
        erhe::ui
        mINI
        Microsoft.GSL::GSL
)

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    target_link_libraries(${_target} PRIVATE erhe_xr openxr_loader)
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
