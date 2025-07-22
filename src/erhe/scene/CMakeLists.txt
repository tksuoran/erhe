set(_target "erhe_scene")
add_library(${_target})
add_library(erhe::scene ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_scene/animation.cpp
    erhe_scene/animation.hpp
    erhe_scene/camera.cpp
    erhe_scene/camera.hpp
    erhe_scene/light.cpp
    erhe_scene/light.hpp
    erhe_scene/mesh.cpp
    erhe_scene/mesh.hpp
    erhe_scene/mesh_raytrace.cpp
    erhe_scene/mesh_raytrace.hpp
    erhe_scene/node.cpp
    erhe_scene/node.hpp
    erhe_scene/node_attachment.cpp
    erhe_scene/node_attachment.hpp
    erhe_scene/projection.cpp
    erhe_scene/projection.hpp
    erhe_scene/scene.cpp
    erhe_scene/scene.hpp
    erhe_scene/scene_host.cpp
    erhe_scene/scene_host.hpp
    erhe_scene/scene_log.cpp
    erhe_scene/scene_log.hpp
    erhe_scene/scene_message.hpp
    erhe_scene/scene_message_bus.cpp
    erhe_scene/scene_message_bus.hpp
    erhe_scene/skin.cpp
    erhe_scene/skin.hpp
    erhe_scene/transform.cpp
    erhe_scene/transform.hpp
    erhe_scene/trs_transform.cpp
    erhe_scene/trs_transform.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    ${_target}
    PUBLIC
        erhe::item
        erhe::math
        erhe::message_bus
        erhe::primitive
        erhe::profile
        glm::glm-header-only
    PRIVATE
        erhe::utility
        # erhe::gl
        erhe::log
        fmt::fmt
)

erhe_target_settings(${_target} "erhe")
