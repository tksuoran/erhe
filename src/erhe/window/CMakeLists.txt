set(_target "erhe_window")
add_library(${_target})
add_library(erhe::window ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_window/renderdoc_capture.cpp
    erhe_window/renderdoc_capture.hpp
    #erhe_window/space_mouse.cpp
    #erhe_window/space_mouse.hpp
    erhe_window/window_log.cpp
    erhe_window/window_log.hpp
    erhe_window/window.cpp
    erhe_window/window.hpp
    erhe_window/window_event_handler.cpp
    erhe_window/window_event_handler.hpp
)
if (${ERHE_WINDOW_LIBRARY} STREQUAL "glfw")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_window/glfw_window.cpp
        erhe_window/glfw_window.hpp
    )
endif()
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
target_link_libraries(
    ${_target}
    PUBLIC
        erhe::time
    PRIVATE
        erhe::gl
        erhe::log
        erhe::profile
        erhe::verify
        fmt::fmt
        glm::glm-header-only
        MathGeoLib
        Microsoft.GSL::GSL
)
#target_link_libraries(${_target} PUBLIC hidapi)
if (ERHE_TARGET_OS_WINDOWS)
    target_link_libraries(${_target} PUBLIC Setupapi.lib)
endif ()
if (${ERHE_WINDOW_LIBRARY} STREQUAL "glfw")
    target_link_libraries(${_target} PUBLIC glfw)
endif ()
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
