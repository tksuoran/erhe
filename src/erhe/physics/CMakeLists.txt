set(_target "erhe_physics")
add_library(${_target})
add_library(erhe::physics ALIAS ${_target})
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
        erhe_physics/jolt/jolt_mesh_shape.cpp
        erhe_physics/jolt/jolt_mesh_shape.hpp
        erhe_physics/jolt/jolt_offset_center_of_mass_shape.cpp
        erhe_physics/jolt/jolt_offset_center_of_mass_shape.hpp
        erhe_physics/jolt/jolt_rigid_body.cpp
        erhe_physics/jolt/jolt_rigid_body.hpp
        erhe_physics/jolt/jolt_scaled_shape.cpp
        erhe_physics/jolt/jolt_scaled_shape.hpp
        erhe_physics/jolt/jolt_uniform_scaling_shape.cpp
        erhe_physics/jolt/jolt_uniform_scaling_shape.hpp
        erhe_physics/jolt/jolt_world.cpp
        erhe_physics/jolt/jolt_world.hpp
        erhe_physics/jolt/glm_conversions.hpp
    )
    set(impl_link_libraries Jolt)
endif ()
if (${ERHE_PHYSICS_LIBRARY} STREQUAL "box3d")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_physics/box3d/box3d_collision_filter_table.cpp
        erhe_physics/box3d/box3d_collision_filter_table.hpp
        erhe_physics/box3d/box3d_collision_shape.cpp
        erhe_physics/box3d/box3d_collision_shape.hpp
        erhe_physics/box3d/box3d_compound_shape.cpp
        erhe_physics/box3d/box3d_compound_shape.hpp
        erhe_physics/box3d/box3d_constraint.cpp
        erhe_physics/box3d/box3d_constraint.hpp
        erhe_physics/box3d/box3d_convex_hull_collision_shape.cpp
        erhe_physics/box3d/box3d_convex_hull_collision_shape.hpp
        erhe_physics/box3d/box3d_material_registry.cpp
        erhe_physics/box3d/box3d_material_registry.hpp
        erhe_physics/box3d/box3d_mesh_shape.cpp
        erhe_physics/box3d/box3d_mesh_shape.hpp
        erhe_physics/box3d/box3d_overlap_query.cpp
        erhe_physics/box3d/box3d_overlap_query.hpp
        erhe_physics/box3d/box3d_rigid_body.cpp
        erhe_physics/box3d/box3d_rigid_body.hpp
        erhe_physics/box3d/box3d_world.cpp
        erhe_physics/box3d/box3d_world.hpp
        erhe_physics/box3d/box3d_wrapper_shapes.cpp
        erhe_physics/box3d/box3d_wrapper_shapes.hpp
        erhe_physics/box3d/box3d_hull_builder.cpp
        erhe_physics/box3d/box3d_hull_builder.hpp
        erhe_physics/box3d/box3d_six_dof_classifier.cpp
        erhe_physics/box3d/box3d_six_dof_classifier.hpp
        erhe_physics/box3d/glm_conversions.hpp
    )
    set(impl_link_libraries box3d::box3d)
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
        erhe_physics/null/null_mesh_shape.cpp
        erhe_physics/null/null_mesh_shape.hpp
        erhe_physics/null/null_offset_center_of_mass_shape.cpp
        erhe_physics/null/null_offset_center_of_mass_shape.hpp
        erhe_physics/null/null_rigid_body.cpp
        erhe_physics/null/null_rigid_body.hpp
        erhe_physics/null/null_scaled_shape.cpp
        erhe_physics/null/null_scaled_shape.hpp
        erhe_physics/null/null_uniform_scaling_shape.cpp
        erhe_physics/null/null_uniform_scaling_shape.hpp
        erhe_physics/null/null_world.cpp
        erhe_physics/null/null_world.hpp
    )
endif ()
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_physics/collision_filter.cpp
    erhe_physics/collision_filter.hpp
    erhe_physics/icollision_shape.hpp
    erhe_physics/iconstraint.hpp
    erhe_physics/iconvex_hull_collision_shape.hpp
    erhe_physics/idebug_draw.hpp
    erhe_physics/irigid_body.cpp
    erhe_physics/irigid_body.hpp
    erhe_physics/iworld.hpp
    erhe_physics/physics_joint_settings.cpp
    erhe_physics/physics_joint_settings.hpp
    erhe_physics/physics_log.cpp
    erhe_physics/physics_log.hpp
    erhe_physics/physics_material.cpp
    erhe_physics/physics_material.hpp
    erhe_physics/transform.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    ${_target}
    PUBLIC
        ${impl_link_libraries}
        erhe::geometry
        erhe::item
        erhe::log
        erhe::primitive
        erhe::profile
        erhe::renderer
        fmt::fmt
        glm::glm-header-only
)

erhe_target_settings(${_target} "erhe")

# The test suite covers the box3d backend: its pure-logic pieces (collision
# filter interning, hull point generation, six-DOF joint classification) and its
# world-level behavior (activation and sensor events, trial-placement overlap
# queries), which step a real Box3d_world. It exists only for that backend --
# the jolt and null backends have no equivalent standalone logic to test.
if ((${ERHE_BUILD_TESTS} STREQUAL "ON") AND (${ERHE_PHYSICS_LIBRARY} STREQUAL "box3d"))
    add_subdirectory(test)
endif ()
