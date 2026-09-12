// The UsdPhysics content of a USD file (doc/usd_compatibility.md, "Physics"):
// what the reader puts into the format-neutral
// `erhe::scene::Physics_description` and into the USD-side record beside it.

#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/physics_description.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto load(const std::filesystem::path& path, const std::shared_ptr<erhe::scene::Node>& root) -> erhe::usd::Usd_load_result
{
    const erhe::usd::Usd_load_arguments arguments{
        .path          = path,
        .root_node     = root,
        .mesh_layer_id = 0
    };
    return erhe::usd::load_usd(arguments);
}

constexpr float c_tolerance = 1e-4f;

class Physics_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root   = std::make_shared<erhe::scene::Xform>("import_root");
        result = load(test_data_path("physics.usda"), root);
        ASSERT_TRUE(result.error.empty()) << result.error;
    }

    // The `Physics_description::node_physics` entry of the prim at
    // `stage_path`, through the record that says where each entry sits.
    [[nodiscard]] auto body_index(const std::string& stage_path) const -> std::size_t
    {
        const std::vector<erhe::usd::Usd_physics_record>& bodies = result.data.physics_prims.bodies;
        for (std::size_t i = 0, end = bodies.size(); i < end; ++i) {
            if (bodies[i].stage_path == stage_path) {
                return i;
            }
        }
        return bodies.size();
    }

    [[nodiscard]] auto body(const std::string& stage_path) const -> const erhe::scene::Physics_node_description*
    {
        const std::size_t index = body_index(stage_path);
        return (index < result.data.physics.node_physics.size())
            ? &result.data.physics.node_physics[index]
            : nullptr;
    }

    [[nodiscard]] auto joint_index_by_name(const std::string& name) const -> std::size_t
    {
        const std::vector<erhe::scene::Physics_joint_description>& joints = result.data.physics.joints;
        for (std::size_t i = 0, end = joints.size(); i < end; ++i) {
            if (joints[i].name == name) {
                return i;
            }
        }
        return joints.size();
    }

    [[nodiscard]] auto property_value(
        const erhe::usd::Usd_physics_record& record,
        const std::string&                   name
    ) const -> std::string
    {
        for (const erhe::usd::Usd_physics_property& property : record.properties) {
            if (property.name == name) {
                return property.value;
            }
        }
        return std::string{};
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Physics_import, physics_scene_gravity)
{
    const erhe::usd::Usd_physics_scene& scene = result.data.physics_prims.scene;
    ASSERT_TRUE(scene.present);
    EXPECT_EQ(scene.stage_path, "/World/PhysicsScene");
    ASSERT_TRUE(scene.gravity_direction.has_value());
    EXPECT_NEAR(scene.gravity_direction.value().y, -1.0f, c_tolerance);
    ASSERT_TRUE(scene.gravity_magnitude.has_value());
    EXPECT_NEAR(scene.gravity_magnitude.value(), 9.81f, c_tolerance);
}

TEST_F(Physics_import, physics_material)
{
    ASSERT_EQ(result.data.physics.materials.size(), 1u);
    const erhe::scene::Physics_material_description& material = result.data.physics.materials.front();
    EXPECT_EQ(material.name, "Rubber");
    EXPECT_NEAR(material.static_friction,  0.9f, c_tolerance);
    EXPECT_NEAR(material.dynamic_friction, 0.8f, c_tolerance);
    EXPECT_NEAR(material.restitution,      0.4f, c_tolerance);
    EXPECT_EQ(material.friction_combine,    erhe::scene::Physics_combine_mode::e_maximum);
    EXPECT_EQ(material.restitution_combine, erhe::scene::Physics_combine_mode::e_minimum);

    ASSERT_EQ(result.data.physics_prims.materials.size(), 1u);
    const erhe::usd::Usd_physics_record& record = result.data.physics_prims.materials.front();
    EXPECT_EQ(record.stage_path, "/World/Looks/Rubber");
    EXPECT_EQ(property_value(record, "Physics_material.density"), "1100");
    EXPECT_EQ(property_value(record, "Physics_material.linear_damping"), "0.25");
}

TEST_F(Physics_import, collision_groups_both_ways)
{
    ASSERT_EQ(result.data.physics.collision_filters.size(), 2u);
    const erhe::scene::Physics_collision_filter_description& props = result.data.physics.collision_filters[0];
    EXPECT_EQ(props.name, "Props");
    ASSERT_EQ(props.collision_systems.size(), 1u);
    EXPECT_EQ(props.collision_systems.front(), "Props");
    ASSERT_EQ(props.not_collide_with_systems.size(), 1u);
    EXPECT_EQ(props.not_collide_with_systems.front(), "Characters");
    EXPECT_TRUE(props.collide_with_systems.empty());

    const erhe::scene::Physics_collision_filter_description& characters = result.data.physics.collision_filters[1];
    EXPECT_EQ(characters.name, "Characters");
    ASSERT_EQ(characters.collision_systems.size(), 1u);
    EXPECT_EQ(characters.collision_systems.front(), "characters");
    ASSERT_EQ(characters.collide_with_systems.size(), 2u);
    EXPECT_EQ(characters.collide_with_systems[0], "props");
    EXPECT_EQ(characters.collide_with_systems[1], "terrain");

    EXPECT_EQ(result.data.physics_prims.collision_filters.size(), 2u);
    EXPECT_EQ(result.data.physics_prims.collision_filters[0].stage_path, "/World/Props");
}

TEST_F(Physics_import, dynamic_body_motion)
{
    const erhe::scene::Physics_node_description* crate = body("/World/Crate");
    ASSERT_NE(crate, nullptr);
    ASSERT_TRUE(crate->node.operator bool());
    EXPECT_EQ(crate->node->get_name(), "Crate");
    ASSERT_TRUE(crate->motion.has_value());
    const erhe::scene::Physics_node_motion& motion = crate->motion.value();
    EXPECT_FALSE(motion.is_kinematic);
    ASSERT_TRUE(motion.mass.has_value());
    EXPECT_NEAR(motion.mass.value(), 12.0f, c_tolerance);
    EXPECT_NEAR(motion.center_of_mass.y,   0.25f, c_tolerance);
    EXPECT_NEAR(motion.linear_velocity.x,  1.0f,  c_tolerance);
    // USD spells an angular velocity in degrees per second.
    EXPECT_NEAR(motion.angular_velocity.y, 1.5708f, 1e-3f);
    EXPECT_NEAR(motion.gravity_factor,     1.0f,  c_tolerance);
}

TEST_F(Physics_import, box_collider_shape_material_and_filter)
{
    const erhe::scene::Physics_node_description* collider_prim = body("/World/Crate/collider");
    ASSERT_NE(collider_prim, nullptr);
    EXPECT_FALSE(collider_prim->motion.has_value());
    ASSERT_TRUE(collider_prim->collider.has_value());
    const erhe::scene::Physics_node_collider& collider = collider_prim->collider.value();
    ASSERT_TRUE(collider.geometry.shape_index.has_value());
    const erhe::scene::Physics_shape& shape = result.data.physics.shapes[collider.geometry.shape_index.value()];
    EXPECT_EQ(shape.type, erhe::scene::Physics_shape_type::e_box);
    EXPECT_NEAR(shape.size.x, 2.0f, c_tolerance);
    EXPECT_NEAR(shape.size.y, 2.0f, c_tolerance);
    EXPECT_NEAR(shape.size.z, 2.0f, c_tolerance);
    // The material is bound on the body prim and the filter names the body
    // prim in its collection; both reach the collider below it.
    ASSERT_TRUE(collider.material_index.has_value());
    EXPECT_EQ(result.data.physics.materials[collider.material_index.value()].name, "Rubber");
    ASSERT_TRUE(collider.filter_index.has_value());
    EXPECT_EQ(result.data.physics.collision_filters[collider.filter_index.value()].name, "Props");
}

TEST_F(Physics_import, kinematic_body_with_sphere_collider)
{
    const erhe::scene::Physics_node_description* ball = body("/World/Ball");
    ASSERT_NE(ball, nullptr);
    ASSERT_TRUE(ball->motion.has_value());
    EXPECT_TRUE(ball->motion.value().is_kinematic);
    EXPECT_FALSE(ball->motion.value().mass.has_value());

    const erhe::scene::Physics_node_description* collider_prim = body("/World/Ball/collider");
    ASSERT_NE(collider_prim, nullptr);
    ASSERT_TRUE(collider_prim->collider.has_value());
    const erhe::scene::Physics_node_geometry& geometry = collider_prim->collider.value().geometry;
    ASSERT_TRUE(geometry.shape_index.has_value());
    const erhe::scene::Physics_shape& shape = result.data.physics.shapes[geometry.shape_index.value()];
    EXPECT_EQ(shape.type, erhe::scene::Physics_shape_type::e_sphere);
    EXPECT_NEAR(shape.radius, 0.75f, c_tolerance);
}

TEST_F(Physics_import, mesh_colliders)
{
    const erhe::scene::Physics_node_description* ground = body("/World/Ground");
    ASSERT_NE(ground, nullptr);
    EXPECT_FALSE(ground->motion.has_value()); // a collider with no body prim above it is a static body
    ASSERT_TRUE(ground->collider.has_value());
    const erhe::scene::Physics_node_geometry& ground_geometry = ground->collider.value().geometry;
    ASSERT_TRUE(ground_geometry.mesh.operator bool());
    EXPECT_EQ(ground_geometry.mesh->get_name(), "Ground");
    EXPECT_FALSE(ground_geometry.convex_hull);
    EXPECT_FALSE(ground_geometry.shape_index.has_value());

    const erhe::scene::Physics_node_description* shell = body("/World/Rock/shell");
    ASSERT_NE(shell, nullptr);
    ASSERT_TRUE(shell->collider.has_value());
    const erhe::scene::Physics_node_geometry& shell_geometry = shell->collider.value().geometry;
    ASSERT_TRUE(shell_geometry.mesh.operator bool());
    EXPECT_TRUE(shell_geometry.convex_hull);

    const erhe::scene::Physics_node_description* rock = body("/World/Rock");
    ASSERT_NE(rock, nullptr);
    EXPECT_TRUE(rock->motion.has_value());
}

TEST_F(Physics_import, erhe_body_values_and_tapered_capsule)
{
    const erhe::scene::Physics_node_description* sensor = body("/World/Sensor");
    ASSERT_NE(sensor, nullptr);
    ASSERT_TRUE(sensor->motion.has_value());
    EXPECT_NEAR(sensor->motion.value().gravity_factor, 0.5f, c_tolerance);
    // A body the file marks as a trigger is a trigger of the description, and
    // the flag is not one of the record's property values.
    EXPECT_TRUE(sensor->trigger.has_value());
    EXPECT_FALSE(sensor->collider.has_value());
    const std::size_t index = body_index("/World/Sensor");
    ASSERT_LT(index, result.data.physics_prims.bodies.size());
    EXPECT_EQ(property_value(result.data.physics_prims.bodies[index], "Node_physics.is_trigger"), "");

    const erhe::scene::Physics_node_description* collider_prim = body("/World/Sensor/collider");
    ASSERT_NE(collider_prim, nullptr);
    ASSERT_TRUE(collider_prim->collider.has_value());
    const erhe::scene::Physics_node_geometry& geometry = collider_prim->collider.value().geometry;
    ASSERT_TRUE(geometry.shape_index.has_value());
    const erhe::scene::Physics_shape& shape = result.data.physics.shapes[geometry.shape_index.value()];
    EXPECT_EQ(shape.type, erhe::scene::Physics_shape_type::e_capsule);
    EXPECT_NEAR(shape.height,        1.5f, c_tolerance);
    EXPECT_NEAR(shape.radius_bottom, 0.6f, c_tolerance);
    EXPECT_NEAR(shape.radius_top,    0.3f, c_tolerance);
}

TEST_F(Physics_import, a_scaled_box_collider_is_its_scaled_extents)
{
    const erhe::scene::Physics_node_description* collider_prim = body("/World/Slab/collider");
    ASSERT_NE(collider_prim, nullptr);
    ASSERT_TRUE(collider_prim->collider.has_value());
    const erhe::scene::Physics_node_geometry& geometry = collider_prim->collider.value().geometry;
    ASSERT_TRUE(geometry.shape_index.has_value());
    const erhe::scene::Physics_shape& shape = result.data.physics.shapes[geometry.shape_index.value()];
    EXPECT_EQ(shape.type, erhe::scene::Physics_shape_type::e_box);
    EXPECT_NEAR(shape.size.x, 2.0f, c_tolerance);
    EXPECT_NEAR(shape.size.y, 0.5f, c_tolerance);
    EXPECT_NEAR(shape.size.z, 3.0f, c_tolerance);
}

TEST_F(Physics_import, guide_collider_prims_are_listed)
{
    const std::vector<std::shared_ptr<erhe::scene::Node>>& guides = result.data.physics_prims.guide_collider_prims;
    ASSERT_EQ(guides.size(), 4u);
    for (const std::shared_ptr<erhe::scene::Node>& guide : guides) {
        ASSERT_TRUE(guide.operator bool());
        EXPECT_EQ(guide->get_name(), "collider");
    }
}

TEST_F(Physics_import, shared_joint_settings_prim)
{
    const std::size_t index = joint_index_by_name("Hinge_settings");
    ASSERT_LT(index, result.data.physics.joints.size());
    const erhe::scene::Physics_joint_description& joint = result.data.physics.joints[index];

    // The two limit instances say the same thing, so they join into one
    // limit over both angular axes.
    ASSERT_EQ(joint.limits.size(), 1u);
    const erhe::scene::Physics_joint_limit& limit = joint.limits.front();
    EXPECT_TRUE(limit.linear_axes.empty());
    ASSERT_EQ(limit.angular_axes.size(), 2u);
    EXPECT_EQ(limit.angular_axes[0], 0);
    EXPECT_EQ(limit.angular_axes[1], 1);
    ASSERT_TRUE(limit.min.has_value());
    ASSERT_TRUE(limit.max.has_value());
    EXPECT_NEAR(limit.min.value(), -0.785398f, 1e-3f);
    EXPECT_NEAR(limit.max.value(),  0.785398f, 1e-3f);
    ASSERT_TRUE(limit.stiffness.has_value());
    EXPECT_NEAR(limit.stiffness.value(), 120.0f, c_tolerance);
    EXPECT_NEAR(limit.damping, 3.0f, c_tolerance);

    ASSERT_EQ(joint.drives.size(), 1u);
    const erhe::scene::Physics_joint_drive& drive = joint.drives.front();
    EXPECT_EQ(drive.type, erhe::scene::Physics_drive_type::e_linear);
    EXPECT_EQ(drive.mode, erhe::scene::Physics_drive_mode::e_acceleration);
    EXPECT_EQ(drive.axis, 2);
    EXPECT_NEAR(drive.max_force,       500.0f, c_tolerance);
    EXPECT_NEAR(drive.position_target, 0.25f,  c_tolerance);
    EXPECT_NEAR(drive.velocity_target, 1.5f,   c_tolerance);
    EXPECT_NEAR(drive.stiffness,       80.0f,  c_tolerance);
    EXPECT_NEAR(drive.damping,         4.0f,   c_tolerance);

    const std::size_t settings_record = index;
    ASSERT_LT(settings_record, result.data.physics_prims.joint_settings.size());
    EXPECT_EQ(result.data.physics_prims.joint_settings[settings_record].stage_path, "/World/Joints/Hinge_settings");
}

TEST_F(Physics_import, joint_prim_uses_the_settings_it_names)
{
    const erhe::scene::Physics_node_description* crate = body("/World/Crate");
    ASSERT_NE(crate, nullptr);
    ASSERT_TRUE(crate->joint.has_value());
    const erhe::scene::Physics_node_joint& joint = crate->joint.value();
    ASSERT_TRUE(joint.connected_node.operator bool());
    EXPECT_EQ(joint.connected_node->get_name(), "Anchor");
    EXPECT_TRUE(joint.enable_collision);
    EXPECT_EQ(joint.joint_index, joint_index_by_name("Hinge_settings"));
}

TEST_F(Physics_import, revolute_joint_becomes_one_angular_limit)
{
    const erhe::scene::Physics_node_description* ball = body("/World/Ball");
    ASSERT_NE(ball, nullptr);
    ASSERT_TRUE(ball->joint.has_value());
    const erhe::scene::Physics_node_joint& node_joint = ball->joint.value();
    EXPECT_FALSE(node_joint.enable_collision);
    ASSERT_LT(node_joint.joint_index, result.data.physics.joints.size());
    const erhe::scene::Physics_joint_description& joint = result.data.physics.joints[node_joint.joint_index];
    EXPECT_EQ(joint.name, "Door");
    ASSERT_EQ(joint.limits.size(), 1u);
    const erhe::scene::Physics_joint_limit& limit = joint.limits.front();
    EXPECT_TRUE(limit.linear_axes.empty());
    ASSERT_EQ(limit.angular_axes.size(), 1u);
    EXPECT_EQ(limit.angular_axes.front(), 1);
    ASSERT_TRUE(limit.min.has_value());
    ASSERT_TRUE(limit.max.has_value());
    EXPECT_NEAR(limit.min.value(), -1.5708f, 1e-3f);
    EXPECT_NEAR(limit.max.value(),  1.5708f, 1e-3f);
}

TEST_F(Physics_import, fixed_joint_locks_every_axis)
{
    const erhe::scene::Physics_node_description* rock = body("/World/Rock");
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(rock->joint.has_value());
    const erhe::scene::Physics_joint_description& joint = result.data.physics.joints[rock->joint.value().joint_index];
    EXPECT_EQ(joint.name, "Weld");
    ASSERT_EQ(joint.limits.size(), 1u);
    const erhe::scene::Physics_joint_limit& limit = joint.limits.front();
    EXPECT_EQ(limit.linear_axes.size(),  3u);
    EXPECT_EQ(limit.angular_axes.size(), 3u);
    ASSERT_TRUE(limit.min.has_value());
    ASSERT_TRUE(limit.max.has_value());
    EXPECT_NEAR(limit.min.value(), 0.0f, c_tolerance);
    EXPECT_NEAR(limit.max.value(), 0.0f, c_tolerance);
}

} // anonymous namespace
