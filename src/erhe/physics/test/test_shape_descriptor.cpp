#include "erhe_physics/box3d/box3d_collision_shape.hpp"
#include "erhe_physics/icollision_shape.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <vector>

namespace {

using erhe::physics::Axis;
using erhe::physics::Collision_shape_type;
using erhe::physics::ICollision_shape;

[[nodiscard]] auto make_unit_cube_points() -> std::vector<float>
{
    std::vector<float> points;
    points.reserve(8u * 3u);
    for (int corner = 0; corner < 8; ++corner) {
        points.push_back(((corner & 1) != 0) ? 1.0f : -1.0f);
        points.push_back(((corner & 2) != 0) ? 1.0f : -1.0f);
        points.push_back(((corner & 4) != 0) ? 1.0f : -1.0f);
    }
    return points;
}

} // anonymous namespace

// The glTF exporter (src/editor/parsers/gltf_physics_export.cpp) switches on
// get_shape_type() and reads the primitive parameters back out, so every
// backend must round-trip them. This guards that contract.

TEST(shape_descriptor, primitive_introspection_round_trips)
{
    const std::shared_ptr<ICollision_shape> box = ICollision_shape::create_box_shape_shared(glm::vec3{1.0f, 2.0f, 3.0f});
    EXPECT_EQ(box->get_shape_type(), Collision_shape_type::e_box);
    ASSERT_TRUE(box->get_half_extents().has_value());
    EXPECT_FLOAT_EQ(box->get_half_extents().value().y, 2.0f);

    const std::shared_ptr<ICollision_shape> sphere = ICollision_shape::create_sphere_shape_shared(2.5f);
    EXPECT_EQ(sphere->get_shape_type(), Collision_shape_type::e_sphere);
    ASSERT_TRUE(sphere->get_radius().has_value());
    EXPECT_FLOAT_EQ(sphere->get_radius().value(), 2.5f);

    const std::shared_ptr<ICollision_shape> capsule = ICollision_shape::create_capsule_shape_shared(Axis::Z, 0.5f, 3.0f);
    EXPECT_EQ(capsule->get_shape_type(), Collision_shape_type::e_capsule);
    EXPECT_FLOAT_EQ(capsule->get_radius().value(), 0.5f);
    EXPECT_FLOAT_EQ(capsule->get_length().value(), 3.0f);
    EXPECT_EQ(capsule->get_axis().value(), Axis::Z);

    const std::shared_ptr<ICollision_shape> tapered_capsule =
        ICollision_shape::create_tapered_capsule_shape_shared(Axis::Y, 1.0f, 0.25f, 2.0f);
    EXPECT_EQ(tapered_capsule->get_shape_type(), Collision_shape_type::e_tapered_capsule);
    EXPECT_FLOAT_EQ(tapered_capsule->get_bottom_radius().value(), 1.0f);
    EXPECT_FLOAT_EQ(tapered_capsule->get_top_radius().value(),    0.25f);
    EXPECT_FLOAT_EQ(tapered_capsule->get_length().value(),        2.0f);

    const std::shared_ptr<ICollision_shape> cylinder =
        ICollision_shape::create_cylinder_shape_shared(Axis::X, glm::vec3{2.0f, 0.5f, 0.5f});
    EXPECT_EQ(cylinder->get_shape_type(), Collision_shape_type::e_cylinder);
    EXPECT_FLOAT_EQ(cylinder->get_half_extents().value().x, 2.0f);
    EXPECT_EQ(cylinder->get_axis().value(), Axis::X);

    const std::shared_ptr<ICollision_shape> tapered_cylinder =
        ICollision_shape::create_tapered_cylinder_shape_shared(Axis::Y, 1.0f, 0.0f, 2.0f);
    EXPECT_EQ(tapered_cylinder->get_shape_type(), Collision_shape_type::e_tapered_cylinder);
    EXPECT_FLOAT_EQ(tapered_cylinder->get_top_radius().value(), 0.0f) << "a cone is a tapered cylinder with one radius at zero";
}

TEST(shape_descriptor, empty_shape_reports_its_own_type)
{
    // The glTF exporter tests explicitly for e_empty
    // (gltf_physics_export.cpp:388), so it must not be reported as a box.
    const std::shared_ptr<ICollision_shape> empty = ICollision_shape::create_empty_shape_shared();
    EXPECT_EQ(empty->get_shape_type(), Collision_shape_type::e_empty);
}

TEST(shape_descriptor, wrapper_introspection_round_trips)
{
    const std::shared_ptr<ICollision_shape> inner = ICollision_shape::create_sphere_shape_shared(1.0f);

    const std::shared_ptr<ICollision_shape> scaled = ICollision_shape::create_scaled_shape_shared(inner, glm::vec3{1.0f, 2.0f, 3.0f});
    EXPECT_EQ(scaled->get_shape_type(), Collision_shape_type::e_scaled);
    EXPECT_FLOAT_EQ(scaled->get_scale().value().z, 3.0f);
    EXPECT_EQ(scaled->get_inner_shape(), inner);

    const std::shared_ptr<ICollision_shape> uniform = ICollision_shape::create_uniform_scaling_shape_shared(inner, 4.0f);
    EXPECT_EQ(uniform->get_shape_type(), Collision_shape_type::e_uniform_scaling);
    EXPECT_FLOAT_EQ(uniform->get_scale().value().x, 4.0f);
    EXPECT_EQ(uniform->get_inner_shape(), inner);

    const std::shared_ptr<ICollision_shape> offset =
        ICollision_shape::create_offset_center_of_mass_shape_shared(inner, glm::vec3{0.0f, 1.5f, 0.0f});
    EXPECT_EQ(offset->get_shape_type(), Collision_shape_type::e_offset_center_of_mass);
    EXPECT_FLOAT_EQ(offset->get_offset().value().y, 1.5f);
    EXPECT_EQ(offset->get_inner_shape(), inner);
}

TEST(shape_descriptor, compound_children_round_trip)
{
    erhe::physics::Compound_shape_create_info create_info;
    erhe::physics::Compound_child child;
    child.shape     = ICollision_shape::create_sphere_shape_shared(1.0f);
    child.transform = erhe::physics::Transform{glm::mat3{1.0f}, glm::vec3{0.0f, 2.0f, 0.0f}};
    create_info.children.push_back(child);

    const std::shared_ptr<ICollision_shape> compound = ICollision_shape::create_compound_shape_shared(create_info);
    EXPECT_EQ(compound->get_shape_type(), Collision_shape_type::e_compound);
    ASSERT_EQ(compound->get_children().size(), 1u);
    EXPECT_FLOAT_EQ(compound->get_children()[0].transform.origin.y, 2.0f);
    EXPECT_FALSE(compound->is_convex());
}

TEST(shape_descriptor, convex_hull_and_mesh_report_their_types)
{
    const std::vector<float> points = make_unit_cube_points();
    const std::shared_ptr<ICollision_shape> hull =
        ICollision_shape::create_convex_hull_shape_shared(points.data(), 8, 3 * static_cast<int>(sizeof(float)));
    EXPECT_EQ(hull->get_shape_type(), Collision_shape_type::e_convex_hull);
    EXPECT_TRUE(hull->is_convex());

    // A single triangle is the smallest mesh Box3D accepts.
    const float    mesh_points[9] = {0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f, 1.0f};
    const uint32_t mesh_indices[3] = {0, 1, 2};
    const std::shared_ptr<ICollision_shape> mesh =
        ICollision_shape::create_mesh_shape_shared(mesh_points, 3, 3 * static_cast<int>(sizeof(float)), mesh_indices, 1);
    EXPECT_EQ(mesh->get_shape_type(), Collision_shape_type::e_mesh);
    EXPECT_FALSE(mesh->is_convex());
}

TEST(shape_descriptor, mass_properties_match_the_analytic_values)
{
    // Sphere: m = 4/3 pi r^3 at unit density; I = 2/5 m r^2 on each axis.
    const std::shared_ptr<ICollision_shape> sphere = ICollision_shape::create_sphere_shape_shared(2.0f);
    const erhe::physics::Mass_properties sphere_mass = sphere->get_mass_properties();
    const float expected_sphere_mass = (4.0f / 3.0f) * glm::pi<float>() * 8.0f;
    EXPECT_NEAR(sphere_mass.mass, expected_sphere_mass, 1e-2f);
    EXPECT_NEAR(sphere_mass.inertia_tensor[0][0], 0.4f * expected_sphere_mass * 4.0f, 1e-1f);

    // Box: m = 8 * hx * hy * hz. The hull is exact for a box, unlike the
    // tessellated shapes.
    const std::shared_ptr<ICollision_shape> box = ICollision_shape::create_box_shape_shared(glm::vec3{1.0f, 2.0f, 3.0f});
    EXPECT_NEAR(box->get_mass_properties().mass, 8.0f * 1.0f * 2.0f * 3.0f, 1e-3f);
}

TEST(shape_descriptor, calculate_local_inertia_scales_to_the_requested_mass)
{
    const std::shared_ptr<ICollision_shape> box = ICollision_shape::create_box_shape_shared(glm::vec3{1.0f, 1.0f, 1.0f});
    glm::mat4 inertia{0.0f};
    box->calculate_local_inertia(12.0f, inertia);
    // Cube of half extent 1, mass 12: I = m * (h^2 + h^2) / 3 = 12 * 8 / 12 = 8.
    EXPECT_NEAR(inertia[0][0], 8.0f, 1e-3f);
    EXPECT_NEAR(inertia[1][1], 8.0f, 1e-3f);
    EXPECT_NEAR(inertia[2][2], 8.0f, 1e-3f);
}

TEST(shape_descriptor, uniform_scaling_scales_mass_by_the_cube_of_the_factor)
{
    const std::shared_ptr<ICollision_shape> inner  = ICollision_shape::create_box_shape_shared(glm::vec3{1.0f, 1.0f, 1.0f});
    const std::shared_ptr<ICollision_shape> scaled = ICollision_shape::create_uniform_scaling_shape_shared(inner, 2.0f);
    EXPECT_NEAR(scaled->get_mass_properties().mass, 8.0f * inner->get_mass_properties().mass, 1e-2f);
}

TEST(shape_descriptor, compound_mass_uses_the_parallel_axis_theorem)
{
    // Two unit cubes at y = +/-2. Total mass is the sum; the combined inertia
    // about x must exceed the sum of the individual tensors by m * d^2 per
    // child, which is what the parallel axis theorem adds.
    erhe::physics::Compound_shape_create_info create_info;
    for (int i = 0; i < 2; ++i) {
        erhe::physics::Compound_child child;
        child.shape     = ICollision_shape::create_box_shape_shared(glm::vec3{1.0f, 1.0f, 1.0f});
        child.transform = erhe::physics::Transform{glm::mat3{1.0f}, glm::vec3{0.0f, (i == 0) ? -2.0f : 2.0f, 0.0f}};
        create_info.children.push_back(child);
    }
    const std::shared_ptr<ICollision_shape> compound = ICollision_shape::create_compound_shape_shared(create_info);
    const erhe::physics::Mass_properties mass_properties = compound->get_mass_properties();

    const float child_mass = 8.0f; // 2 * 2 * 2 at unit density
    EXPECT_NEAR(mass_properties.mass, 2.0f * child_mass, 1e-2f);
    // Center of mass is midway between the two children.
    EXPECT_NEAR(compound->get_center_of_mass().y, 0.0f, 1e-4f);
    // Each child contributes its own I_xx (m * 8 / 12) plus m * d^2 = 8 * 4.
    const float expected_xx = 2.0f * (((child_mass * 8.0f) / 12.0f) + (child_mass * 4.0f));
    EXPECT_NEAR(mass_properties.inertia_tensor[0][0], expected_xx, 1e-1f);
}
