set(_target "erhe_imgui")
add_library(${_target})
add_library(erhe::imgui ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_imgui/imgui_helpers.cpp
    erhe_imgui/imgui_helpers.hpp
    erhe_imgui/imgui_log.cpp
    erhe_imgui/imgui_log.hpp
    erhe_imgui/imgui_renderer.cpp
    erhe_imgui/imgui_renderer.hpp
    erhe_imgui/imgui_viewport.cpp
    erhe_imgui/imgui_viewport.hpp
    erhe_imgui/imgui_window.cpp
    erhe_imgui/imgui_window.hpp
    erhe_imgui/imgui_windows.cpp
    erhe_imgui/imgui_windows.hpp
    erhe_imgui/scoped_imgui_context.cpp
    erhe_imgui/scoped_imgui_context.hpp
    erhe_imgui/window_imgui_viewport.cpp
    erhe_imgui/window_imgui_viewport.hpp
    erhe_imgui/ImNodes.cpp
    erhe_imgui/ImNodes.h
    erhe_imgui/ImNodesEz.cpp
    erhe_imgui/ImNodesEz.h
    erhe_imgui/windows/framebuffer_window.cpp
    erhe_imgui/windows/framebuffer_window.hpp
    erhe_imgui/windows/log_window.cpp
    erhe_imgui/windows/log_window.hpp
    erhe_imgui/windows/performance_window.cpp
    erhe_imgui/windows/performance_window.hpp
    erhe_imgui/windows/pipelines.cpp
    erhe_imgui/windows/pipelines.hpp
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
target_link_libraries(${_target}
    PUBLIC
        fmt::fmt
        erhe::commands
        erhe::concurrency
        erhe::graphics
        erhe::log
        erhe::math
        erhe::rendergraph
        erhe::window
        glm::glm
        imgui
    PRIVATE
        concurrentqueue
        cxxopts
        etl::etl
        erhe::gl
        erhe::profile
        erhe::renderer
        erhe::time
        Microsoft.GSL::GSL
)
if (${ERHE_PNG_LIBRARY} STREQUAL "mango")
    target_link_libraries(${_target} PUBLIC mango spng)
endif()
if (${ERHE_PNG_LIBRARY} STREQUAL "fpng")
    target_link_libraries(${_target} PUBLIC fpng)
endif()
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
