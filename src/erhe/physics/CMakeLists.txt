set(_target "erhe_physics")

add_library(${_target})

if (${ERHE_PHYSICS_LIBRARY} STREQUAL "bullet")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        bullet/bullet_collision_shape.cpp
        bullet/bullet_collision_shape.hpp
        bullet/bullet_compound_shape.cpp
        bullet/bullet_compound_shape.hpp
        bullet/bullet_constraint.cpp
        bullet/bullet_constraint.hpp
        bullet/bullet_convex_hull_collision_shape.cpp
        bullet/bullet_convex_hull_collision_shape.hpp
        bullet/bullet_rigid_body.cpp
        bullet/bullet_rigid_body.hpp
        bullet/bullet_uniform_scaling_shape.cpp
        bullet/bullet_uniform_scaling_shape.hpp
        bullet/bullet_world.cpp
        bullet/bullet_world.hpp
        bullet/glm_conversions.hpp
    )
    set(impl_link_libraries
        Bullet3Collision
        Bullet3Common
        Bullet3Dynamics
        Bullet3Geometry
        BulletDynamics
        BulletCollision
        LinearMath
    )

endif ()

if (${ERHE_PHYSICS_LIBRARY} STREQUAL "none")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        null/null_collision_shape.cpp
        null/null_collision_shape.hpp
        null/null_compound_shape.cpp
        null/null_compound_shape.hpp
        null/null_constraint.cpp
        null/null_constraint.hpp
        null/null_convex_hull_collision_shape.cpp
        null/null_convex_hull_collision_shape.hpp
        null/null_rigid_body.cpp
        null/null_rigid_body.hpp
        null/null_uniform_scaling_shape.cpp
        null/null_uniform_scaling_shape.hpp
        null/null_world.cpp
        null/null_world.hpp
    )
endif ()

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    log.cpp
    log.hpp
    icollision_shape.hpp
    iconstraint.hpp
    iconvex_hull_collision_shape.hpp
    imotion_state.hpp
    irigid_body.hpp
    iworld.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(
    ${_target}
    PUBLIC
    ${impl_link_libraries}
    erhe_components
    erhe_geometry
    erhe_log
    erhe_primitive
    fmt::fmt
    glm::glm
    optional_lite
    Microsoft.GSL::GSL
)

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
