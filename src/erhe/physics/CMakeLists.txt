set(_target "erhe_physics")

set(source_list
    log.cpp
    log.hpp
    physics_system.cpp
    physics_system.hpp
    rigid_body.cpp
    rigid_body.hpp
    shape.cpp
    shape.hpp
    trigger.cpp
    trigger.hpp
    world.cpp
    world.hpp
)

add_library(${_target} ${source_list})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        Bullet3Collision
        Bullet3Common
        Bullet3Dynamics
        Bullet3Geometry
        BulletCollision
        BulletDynamics
        LinearMath
        erhe_components
        erhe_geometry
        erhe_log
        erhe_primitive
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
        TracyClient
    )

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
