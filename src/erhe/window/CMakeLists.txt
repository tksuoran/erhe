set(_target "erhe_window")
add_library(${_target})
add_library(erhe::window ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_window/renderdoc_capture.cpp
    erhe_window/renderdoc_capture.hpp
    erhe_window/window_log.cpp
    erhe_window/window_log.hpp
    erhe_window/window.cpp
    erhe_window/window.hpp
    erhe_window/window_event_handler.cpp
    erhe_window/window_event_handler.hpp
    erhe_window/window_configuration.hpp
)

if (${ERHE_WINDOW_LIBRARY} STREQUAL "glfw")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_window/glfw_window.cpp
        erhe_window/glfw_window.hpp
    )
endif()

if (${ERHE_WINDOW_LIBRARY} STREQUAL "sdl")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_window/sdl_window.cpp
        erhe_window/sdl_window.hpp
    )
endif()

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (ERHE_GRAPHICS_API_OPENGL)
    target_link_libraries(${_target} PRIVATE erhe::gl)
endif ()

target_link_libraries(
    ${_target}
    PUBLIC
        erhe::time
    PRIVATE
        erhe::log
        erhe::profile
        erhe::verify
        fmt::fmt
        glm::glm-header-only
)

if (ERHE_TARGET_OS_WINDOWS)
    target_link_libraries(${_target} PUBLIC Setupapi.lib)
endif ()

if (${ERHE_WINDOW_LIBRARY} STREQUAL "glfw")
    target_link_libraries(${_target} PUBLIC glfw)
endif ()
if (${ERHE_WINDOW_LIBRARY} STREQUAL "sdl")
    target_link_libraries(${_target} PUBLIC SDL3::SDL3-static)
endif ()

erhe_target_settings(${_target} "erhe")
