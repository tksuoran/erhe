set(_target "erhe_scene")
add_library(${_target})
add_library(erhe::scene ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    animation.cpp
    animation.hpp
    camera.cpp
    camera.hpp
    light.cpp
    light.hpp
    mesh.cpp
    mesh.hpp
    node.cpp
    node.hpp
    node_attachment.cpp
    node_attachment.hpp
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
    skin.cpp
    skin.hpp
    transform.cpp
    transform.hpp
    trs_transform.cpp
    trs_transform.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(
    ${_target}
    PRIVATE
        erhe::item
        erhe::gl
        erhe::log
        erhe::message_bus
        fmt::fmt
        Microsoft.GSL::GSL
    PUBLIC
        erhe::primitive
        glm::glm
)

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
