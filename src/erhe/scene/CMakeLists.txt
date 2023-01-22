set(_target "erhe_scene")
add_library(${_target})
add_library(erhe::scene ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    camera.cpp
    camera.hpp
    item.cpp
    item.hpp
    light.cpp
    light.hpp
    mesh.cpp
    mesh.hpp
    node.cpp
    node.hpp
    projection.cpp
    projection.hpp
    scene.cpp
    scene.hpp
    scene_host.cpp
    scene_host.hpp
    scene_log.cpp
    scene_log.hpp
    scene_message.hpp
    scene_message_bus.cpp
    scene_message_bus.hpp
    transform.cpp
    transform.hpp
    viewport.cpp
    viewport.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(
    ${_target}
    PRIVATE
        erhe::components
        erhe::gl
        erhe::log
        erhe::message_bus
        fmt::fmt
        Microsoft.GSL::GSL
    PUBLIC
        erhe::primitive
        glm::glm
)

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
