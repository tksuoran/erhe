set(_target erhe_application)
add_library(${_target})
add_library(erhe::application ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    commands/command.cpp
    commands/command.hpp
    commands/command_binding.cpp
    commands/command_binding.hpp
    commands/command_context.cpp
    commands/command_context.hpp
    commands/key_binding.cpp
    commands/key_binding.hpp
    commands/mouse_binding.cpp
    commands/mouse_binding.hpp
    commands/mouse_click_binding.cpp
    commands/mouse_click_binding.hpp
    commands/mouse_motion_binding.cpp
    commands/mouse_motion_binding.hpp
    commands/mouse_drag_binding.cpp
    commands/mouse_drag_binding.hpp
    commands/mouse_wheel_binding.cpp
    commands/mouse_wheel_binding.hpp
    commands/state.cpp
    commands/state.hpp

    graphics/gl_context_provider.cpp
    graphics/gl_context_provider.hpp
    graphics/shader_monitor.cpp
    graphics/shader_monitor.hpp

    renderers/buffer_writer.hpp
    renderers/line_renderer.cpp
    renderers/line_renderer.hpp
    renderers/quad_renderer.cpp
    renderers/quad_renderer.hpp
    renderers/text_renderer.cpp
    renderers/text_renderer.hpp

    windows/framebuffer_window.cpp
    windows/framebuffer_window.hpp
    windows/imgui_window.cpp
    windows/imgui_window.hpp
    windows/log_window.cpp
    windows/log_window.hpp
    windows/performance_window.cpp
    windows/performance_window.hpp
    windows/pipelines.cpp
    windows/pipelines.hpp

    application.cpp
    application.hpp
    configuration.cpp
    configuration.hpp
    controller.cpp
    controller.hpp
    imgui_viewport.cpp
    imgui_viewport.hpp
    imgui_windows.cpp
    imgui_windows.hpp
    log.cpp
    log.hpp
    renderdoc_capture_support.cpp
    renderdoc_capture_support.hpp
    scoped_imgui_context.cpp
    scoped_imgui_context.hpp
    time.cpp
    time.hpp
    view.cpp
    view.hpp
    window.cpp
    window.hpp
    window_imgui_viewport.cpp
    window_imgui_viewport.hpp
)

if (${ERHE_GUI_LIBRARY} STREQUAL "imgui")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        renderers/imgui_renderer.cpp
        renderers/imgui_renderer.hpp
        windows/imgui_demo_window.cpp
        windows/imgui_demo_window.hpp
        imgui_helpers.cpp
        imgui_helpers.hpp
    )
    target_link_libraries(${_target}
    PUBLIC
        imgui
)

endif ()

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        cxxopts
        fmt::fmt
        erhe::concurrency
        erhe::gl
        erhe::graphics
        erhe::log
        erhe::toolkit
        erhe::ui
        mINI
        ghc_filesystem
        optional_lite
        Microsoft.GSL::GSL
)

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
