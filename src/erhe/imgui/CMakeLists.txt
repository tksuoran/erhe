set(_target "erhe_imgui")
add_library(${_target})
add_library(erhe::imgui ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_imgui/file_dialog.cpp
    erhe_imgui/file_dialog.hpp
    erhe_imgui/imgui_helpers.cpp
    erhe_imgui/imgui_helpers.hpp
    erhe_imgui/imgui_log.cpp
    erhe_imgui/imgui_log.hpp
    erhe_imgui/imgui_renderer.cpp
    erhe_imgui/imgui_renderer.hpp
    erhe_imgui/imgui_host.cpp
    erhe_imgui/imgui_host.hpp
    erhe_imgui/imgui_window.cpp
    erhe_imgui/imgui_window.hpp
    erhe_imgui/imgui_windows.cpp
    erhe_imgui/imgui_windows.hpp
    erhe_imgui/scoped_imgui_context.cpp
    erhe_imgui/scoped_imgui_context.hpp
    erhe_imgui/window_imgui_host.cpp
    erhe_imgui/window_imgui_host.hpp

    erhe_imgui/crude_json.cpp
    erhe_imgui/crude_json.h
    erhe_imgui/imgui_bezier_math.h
    erhe_imgui/imgui_bezier_math.inl
    erhe_imgui/imgui_canvas.cpp
    erhe_imgui/imgui_canvas.h
    erhe_imgui/imgui_extra_math.h
    erhe_imgui/imgui_extra_math.inl
    erhe_imgui/imgui_node_editor.cpp
    erhe_imgui/imgui_node_editor.h
    erhe_imgui/imgui_node_editor_api.cpp
    erhe_imgui/imgui_node_editor_internal.h
    erhe_imgui/imgui_node_editor_internal.inl

    erhe_imgui/windows/framebuffer_window.cpp
    erhe_imgui/windows/framebuffer_window.hpp
    erhe_imgui/windows/log_window.cpp
    erhe_imgui/windows/log_window.hpp
    erhe_imgui/windows/performance_window.cpp
    erhe_imgui/windows/performance_window.hpp
    erhe_imgui/windows/pipelines.cpp
    erhe_imgui/windows/pipelines.hpp
    erhe_imgui/windows/graph.cpp
    erhe_imgui/windows/graph.hpp
    erhe_imgui/windows/graph_plotter.cpp
    erhe_imgui/windows/graph_plotter.hpp
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
target_link_libraries(${_target}
    PUBLIC
        fmt::fmt
        erhe::commands
        erhe::graphics
        erhe::log
        erhe::math
        erhe::rendergraph
        erhe::window
        glm::glm-header-only
        imgui
    PRIVATE
        concurrentqueue
        cxxopts
        etl::etl
        erhe::gl
        erhe::profile
        erhe::renderer
        erhe::time
)
target_link_libraries(${_target} PUBLIC mango)
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
