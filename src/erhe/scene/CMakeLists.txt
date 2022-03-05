set(_target "erhe_scene")
add_library(${_target})
add_library(erhe::scene ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    camera.cpp
    camera.hpp
    node.cpp
    node.hpp
    mesh.cpp
    mesh.hpp
    light.cpp
    light.hpp
    log.cpp
    log.hpp
    projection.cpp
    projection.hpp
    scene.cpp
    scene.hpp
    transform.cpp
    transform.hpp
    viewport.cpp
    viewport.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(
    ${_target}
    PRIVATE
        erhe::gl
        fmt::fmt
        Microsoft.GSL::GSL
    PUBLIC
        erhe::primitive
        optional_lite
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
