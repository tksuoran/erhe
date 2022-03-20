set(_target "erhe_toolkit")
add_library(${_target})
add_library(erhe::toolkit ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    file.cpp
    file.hpp
    filesystem.hpp
    math_util.cpp
    math_util.hpp
    optional.hpp
    profile.hpp
    timer.cpp
    timer.hpp
    timestamp.cpp
    timestamp.hpp
    verify.hpp
    view.cpp
    view.hpp
    window.hpp
)

if (CMAKE_SYSTEM_NAME MATCHES "(Windows|MSYS)")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        space_mouse.cpp
        space_mouse.hpp
    )
endif ()

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
    PUBLIC
        ghc_filesystem
        optional_lite
    PRIVATE
        erhe::gl
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
)

if (CMAKE_SYSTEM_NAME MATCHES "(Windows|MSYS)")
    target_link_libraries(
        ${_target}
        PUBLIC
        hidapi
    )
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
