set(_target "erhe_physics")

message("physics library = ${ERHE_PHYSICS_LIBRARY}")

if (${ERHE_PHYSICS_LIBRARY} STREQUAL "bullet")
    message("using bullet")
    set(impl_source_list
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
        BulletCollision
        BulletDynamics
        LinearMath
    )

endif ()

if (${ERHE_PHYSICS_LIBRARY} STREQUAL "none")
    message("using none")
    set(impl_source_list
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

set(source_list
    log.cpp
    log.hpp
    icollision_shape.hpp
    iconstraint.hpp
    iconvex_hull_collision_shape.hpp
    imotion_state.hpp
    irigid_body.hpp
    iworld.hpp
)

message("impl_source_list = ${impl_source_list}")
message("impl_link_libraries = ${impl_link_libraries}")

add_library(${_target} ${source_list} ${impl_source_list})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        ${impl_link_libraries}
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
