set(_target "erhe_imgui")
add_library(${_target})
add_library(erhe::imgui ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    imgui_helpers.cpp
    imgui_helpers.hpp
    imgui_log.cpp
    imgui_log.hpp
    imgui_renderer.cpp
    imgui_renderer.hpp
    imgui_viewport.cpp
    imgui_viewport.hpp
    imgui_window.cpp
    imgui_window.hpp
    imgui_windows.cpp
    imgui_windows.hpp
    scoped_imgui_context.cpp
    scoped_imgui_context.hpp
    window_imgui_viewport.cpp
    window_imgui_viewport.hpp
    ImNodes.cpp
    ImNodes.h
    ImNodesEz.cpp
    ImNodesEz.h
    windows/framebuffer_window.cpp
    windows/framebuffer_window.hpp
    windows/log_window.cpp
    windows/log_window.hpp
    windows/performance_window.cpp
    windows/performance_window.hpp
    windows/pipelines.cpp
    windows/pipelines.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        concurrentqueue
        cxxopts
        etl::etl
        fmt::fmt
        erhe::commands
        erhe::concurrency
        erhe::gl
        erhe::graphics
        erhe::log
        erhe::toolkit
        erhe::ui
        imgui
        mINI
        Microsoft.GSL::GSL
)

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

if (${ERHE_PNG_LIBRARY} STREQUAL "mango")
    target_link_libraries(${_target} PUBLIC mango spng)
endif()
if (${ERHE_PNG_LIBRARY} STREQUAL "fpng")
    target_link_libraries(${_target} PUBLIC fpng)
endif()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
