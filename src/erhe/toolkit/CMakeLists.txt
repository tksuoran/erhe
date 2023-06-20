set(_target "erhe_toolkit")
add_library(${_target})
add_library(erhe::toolkit ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    defer.cpp
    defer.hpp
    file.cpp
    file.hpp
    hash.cpp
    hash.hpp
    math_util.cpp
    math_util.hpp
    profile.hpp
    renderdoc_capture.cpp
    renderdoc_capture.hpp
    simulation_variable.cpp
    simulation_variable.hpp
    sleep.cpp
    sleep.hpp
    space_mouse.cpp
    space_mouse.hpp
    timer.cpp
    timer.hpp
    timestamp.cpp
    timestamp.hpp
    toolkit_log.cpp
    toolkit_log.hpp
    unique_id.hpp
    verify.hpp
    viewport.cpp
    viewport.hpp
    window.cpp
    window.hpp
    window_event_handler.cpp
    window_event_handler.hpp
)

if (${ERHE_WINDOW_LIBRARY} STREQUAL "glfw")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
         glfw_window.cpp
         glfw_window.hpp
    )
endif()

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(
    ${_target}
    PRIVATE
        erhe::gl
        erhe::log
        fmt::fmt
        glm::glm
        MathGeoLib
        Microsoft.GSL::GSL
)

target_link_libraries(
    ${_target}
    PUBLIC
    hidapi
)

if (ERHE_TARGET_OS_WINDOWS)
    target_link_libraries(${_target} PUBLIC Setupapi.lib)
endif ()

if (${ERHE_WINDOW_LIBRARY} STREQUAL "glfw")
    target_link_libraries(${_target} PUBLIC glfw)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
