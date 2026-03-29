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

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

# TODO Get rid of this dependency - for user clip enable/disable
if (ERHE_GRAPHICS_API_OPENGL)
    target_link_libraries(${_target} PRIVATE erhe::gl)
endif ()

target_link_libraries(${_target}
    PUBLIC
        fmt::fmt
        erhe::commands
        erhe::defer
        erhe::graphics
        erhe::log
        erhe::math
        erhe::rendergraph
        erhe::window
        glm::glm-header-only
        imgui
    PRIVATE
        erhe::codegen
        erhe::file
        erhe::profile
        erhe::renderer
        erhe::time
)

target_link_libraries(${_target} PUBLIC mango)

# Codegen for window visibility config
set(_imgui_defs "${CMAKE_CURRENT_SOURCE_DIR}/definitions")
set(_imgui_gen  "${CMAKE_CURRENT_BINARY_DIR}/erhe_imgui/generated")

erhe_codegen_generate(
    TARGET          ${_target}
    DEFINITIONS_DIR "${_imgui_defs}"
    OUTPUT_DIR      "${_imgui_gen}"
    DEFINITIONS
        "${_imgui_defs}/logger_entry.py"
        "${_imgui_defs}/logging_config.py"
        "${_imgui_defs}/window_visibility_entry.py"
        "${_imgui_defs}/windows_visibility_config.py"
)

set(_imgui_codegen_sources
    ${_imgui_gen}/logger_entry.hpp
    ${_imgui_gen}/logger_entry_serialization.hpp
    ${_imgui_gen}/logger_entry.cpp
    ${_imgui_gen}/logging_config.hpp
    ${_imgui_gen}/logging_config_serialization.hpp
    ${_imgui_gen}/logging_config.cpp
    ${_imgui_gen}/window_visibility_entry.hpp
    ${_imgui_gen}/window_visibility_entry_serialization.hpp
    ${_imgui_gen}/window_visibility_entry.cpp
    ${_imgui_gen}/windows_visibility_config.hpp
    ${_imgui_gen}/windows_visibility_config_serialization.hpp
    ${_imgui_gen}/windows_visibility_config.cpp
)
set_source_files_properties(${_imgui_codegen_sources} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_imgui_codegen_sources})
source_group("codegen generated" FILES ${_imgui_codegen_sources})

erhe_target_settings(${_target} "erhe")
