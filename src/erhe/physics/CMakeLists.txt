set(_target "erhe_physics")
add_library(${_target})
add_library(erhe::physics ALIAS ${_target})
if (${ERHE_PHYSICS_LIBRARY} STREQUAL "bullet")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}/erhe_physics" FILES
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
if (${ERHE_PHYSICS_LIBRARY} STREQUAL "jolt")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_physics/jolt/jolt_collision_shape.cpp
        erhe_physics/jolt/jolt_collision_shape.hpp
        erhe_physics/jolt/jolt_compound_shape.cpp
        erhe_physics/jolt/jolt_compound_shape.hpp
        erhe_physics/jolt/jolt_constraint.cpp
        erhe_physics/jolt/jolt_constraint.hpp
        erhe_physics/jolt/jolt_convex_hull_collision_shape.cpp
        erhe_physics/jolt/jolt_convex_hull_collision_shape.hpp
        erhe_physics/jolt/jolt_debug_renderer.cpp
        erhe_physics/jolt/jolt_debug_renderer.hpp
        erhe_physics/jolt/jolt_rigid_body.cpp
        erhe_physics/jolt/jolt_rigid_body.hpp
        erhe_physics/jolt/jolt_uniform_scaling_shape.cpp
        erhe_physics/jolt/jolt_uniform_scaling_shape.hpp
        erhe_physics/jolt/jolt_world.cpp
        erhe_physics/jolt/jolt_world.hpp
        erhe_physics/jolt/glm_conversions.hpp
    )
    set(impl_link_libraries Jolt)
endif ()
if (${ERHE_PHYSICS_LIBRARY} STREQUAL "none")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_physics/null/null_collision_shape.cpp
        erhe_physics/null/null_collision_shape.hpp
        erhe_physics/null/null_compound_shape.cpp
        erhe_physics/null/null_compound_shape.hpp
        erhe_physics/null/null_constraint.cpp
        erhe_physics/null/null_constraint.hpp
        erhe_physics/null/null_convex_hull_collision_shape.cpp
        erhe_physics/null/null_convex_hull_collision_shape.hpp
        erhe_physics/null/null_rigid_body.cpp
        erhe_physics/null/null_rigid_body.hpp
        erhe_physics/null/null_uniform_scaling_shape.cpp
        erhe_physics/null/null_uniform_scaling_shape.hpp
        erhe_physics/null/null_world.cpp
        erhe_physics/null/null_world.hpp
    )
endif ()
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_physics/icollision_shape.hpp
    erhe_physics/iconstraint.hpp
    erhe_physics/iconvex_hull_collision_shape.hpp
    erhe_physics/idebug_draw.hpp
    erhe_physics/irigid_body.hpp
    erhe_physics/iworld.hpp
    erhe_physics/physics_log.cpp
    erhe_physics/physics_log.hpp
    erhe_physics/transform.hpp
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
target_link_libraries(
    ${_target}
    PUBLIC
        ${impl_link_libraries}
        erhe::geometry
        erhe::log
        erhe::primitive
        erhe::profile
        fmt::fmt
        glm::glm-header-only
)
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
