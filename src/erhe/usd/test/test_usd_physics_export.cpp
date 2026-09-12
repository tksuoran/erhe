// The UsdPhysics write (doc/usd_compatibility.md, "Physics"): the physics of
// a loaded file, written back and read again, is the physics it was, and the
// second write of it is byte for byte the first.

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/physics_description.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_physics_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::string
{
    std::ifstream stream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

constexpr float c_tolerance = 1e-3f;

// The physics of one loaded file in the form a save takes it in: the body of
// a guide collider prim is the prim the collider hangs below, so the shapes
// those prims carry fold onto their body the way the editor's physics import
// folds them, and the prims themselves leave the tree. That is the contract
// the reader's `guide_collider_prims` states, and what a scene the editor
// holds looks like.
class Save_physics_input final
{
public:
    erhe::scene::Physics_description description;
    erhe::usd::Usd_save_physics      physics;
};

[[nodiscard]] auto is_guide_prim(const erhe::usd::Usd_data& data, const erhe::scene::Node* node) -> bool
{
    for (const std::shared_ptr<erhe::scene::Node>& guide : data.physics_prims.guide_collider_prims) {
        if (guide.get() == node) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto find_item_at(const std::shared_ptr<erhe::scene::Node>& root, const std::string& stage_path) -> std::shared_ptr<erhe::Item_base>
{
    if (stage_path.empty()) {
        return {};
    }
    erhe::Hierarchy* const found = erhe::find_by_path(*root.get(), std::string_view{stage_path}.substr(1));
    if (found == nullptr) {
        return {};
    }
    return std::static_pointer_cast<erhe::Item_base>(found->shared_from_this());
}

[[nodiscard]] auto make_records(
    const std::vector<erhe::usd::Usd_physics_record>& records,
    const std::shared_ptr<erhe::scene::Node>&         root
) -> std::vector<erhe::usd::Usd_save_physics_record>
{
    std::vector<erhe::usd::Usd_save_physics_record> out;
    out.reserve(records.size());
    for (const erhe::usd::Usd_physics_record& record : records) {
        out.push_back(
            erhe::usd::Usd_save_physics_record{
                .item       = find_item_at(root, record.stage_path),
                .properties = record.properties
            }
        );
    }
    return out;
}

// The collider prim's own scale, in the dimensions of the shape it carries:
// what the editor's physics import applies as the collider node's transform
// when it folds the shape into its body, and what a save has to see for the
// prim it writes to state the same size again.
[[nodiscard]] auto scaled_shape(
    const erhe::scene::Physics_shape& shape,
    const glm::vec3&                  scale
) -> erhe::scene::Physics_shape
{
    erhe::scene::Physics_shape out = shape;
    out.size          = shape.size * scale;
    out.radius        = shape.radius        * scale.x;
    out.radius_bottom = shape.radius_bottom * scale.x;
    out.radius_top    = shape.radius_top    * scale.x;
    out.height        = shape.height        * scale.y;
    return out;
}

[[nodiscard]] auto build_save_input(
    const erhe::usd::Usd_data&                data,
    const std::shared_ptr<erhe::scene::Node>& root
) -> std::unique_ptr<Save_physics_input>
{
    std::unique_ptr<Save_physics_input> input = std::make_unique<Save_physics_input>();
    input->description.shapes            = data.physics.shapes;
    input->description.materials         = data.physics.materials;
    input->description.collision_filters = data.physics.collision_filters;
    input->description.joints            = data.physics.joints;

    // The entries whose prim is a guide collider fold onto the body above
    // them; every other entry is kept in the order it was read.
    std::vector<std::size_t> kept;
    for (std::size_t index = 0, end = data.physics.node_physics.size(); index < end; ++index) {
        if (!is_guide_prim(data, data.physics.node_physics[index].node.get())) {
            kept.push_back(index);
        }
    }
    for (const std::size_t index : kept) {
        input->description.node_physics.push_back(data.physics.node_physics[index]);
        input->physics.bodies.push_back(
            erhe::usd::Usd_save_physics_record{
                .properties = (index < data.physics_prims.bodies.size())
                    ? data.physics_prims.bodies[index].properties
                    : std::vector<erhe::usd::Usd_physics_property>{}
            }
        );
    }
    for (std::size_t index = 0, end = data.physics.node_physics.size(); index < end; ++index) {
        const erhe::scene::Physics_node_description& entry = data.physics.node_physics[index];
        if (!is_guide_prim(data, entry.node.get()) || !entry.collider.has_value()) {
            continue;
        }
        const erhe::scene::Node* const parent = entry.node->get_parent_node().get();
        for (std::size_t body = 0, body_end = input->description.node_physics.size(); body < body_end; ++body) {
            erhe::scene::Physics_node_description& body_entry = input->description.node_physics[body];
            if (body_entry.node.get() != parent) {
                continue;
            }
            erhe::scene::Physics_node_collider folded = entry.collider.value();
            const glm::vec3 scale = entry.node->parent_from_node_transform().get_scale();
            if (folded.geometry.shape_index.has_value() && (scale != glm::vec3{1.0f})) {
                const erhe::scene::Physics_shape& shape =
                    input->description.shapes[folded.geometry.shape_index.value()];
                input->description.shapes.push_back(scaled_shape(shape, scale));
                folded.geometry.shape_index = input->description.shapes.size() - 1;
            }
            if (body_entry.trigger.has_value()) {
                // A trigger body detects overlaps with the same shapes a
                // collider body collides with.
                body_entry.trigger.value().geometry     = folded.geometry;
                body_entry.trigger.value().filter_index = folded.filter_index;
            } else {
                body_entry.collider = folded;
            }
            break;
        }
    }
    // The guide prims are not scene content once their shapes have folded.
    for (const std::shared_ptr<erhe::scene::Node>& guide : data.physics_prims.guide_collider_prims) {
        if (guide) {
            guide->erhe::Hierarchy::set_parent(std::shared_ptr<erhe::Hierarchy>{});
        }
    }

    input->physics.description       = &input->description;
    input->physics.materials         = make_records(data.physics_prims.materials,         root);
    input->physics.collision_filters = make_records(data.physics_prims.collision_filters, root);
    input->physics.joint_settings    = make_records(data.physics_prims.joint_settings,    root);
    input->physics.has_physics_scene = data.physics_prims.scene.present;
    input->physics.gravity_direction = data.physics_prims.scene.gravity_direction;
    input->physics.gravity_magnitude = data.physics_prims.scene.gravity_magnitude;
    return input;
}

// Load `physics.usda`, write its physics back out, load that, and write the
// result a second time: the two written files are what the fixed point is
// asserted on.
class Physics_round_trip
{
public:
    Physics_round_trip()
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("physics.usda"),
            .root_node     = source_root,
            .mesh_layer_id = 0
        };
        source = erhe::usd::load_usd(load_arguments);
        if (!source.error.empty()) {
            return;
        }

        source_input = build_save_input(source.data, source_root);
        first_path   = temporary_path("physics_first.usda");
        const erhe::usd::Usd_save_arguments first_arguments{
            .path      = first_path,
            .root_node = source_root,
            .materials = source.data.materials,
            .physics   = source_input->physics
        };
        first_save = erhe::usd::save_usda(first_arguments);
        if (!first_save.error.empty()) {
            return;
        }

        reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
        const erhe::usd::Usd_load_arguments reload_arguments{
            .path          = first_path,
            .root_node     = reloaded_root,
            .mesh_layer_id = 0
        };
        reloaded = erhe::usd::load_usd(reload_arguments);
        if (!reloaded.error.empty()) {
            return;
        }

        reloaded_input = build_save_input(reloaded.data, reloaded_root);
        second_path    = temporary_path("physics_second.usda");
        const erhe::usd::Usd_save_arguments second_arguments{
            .path      = second_path,
            .root_node = reloaded_root,
            .materials = reloaded.data.materials,
            .physics   = reloaded_input->physics
        };
        second_save = erhe::usd::save_usda(second_arguments);
    }

    std::shared_ptr<erhe::scene::Node>  source_root;
    std::shared_ptr<erhe::scene::Node>  reloaded_root;
    std::unique_ptr<Save_physics_input> source_input;
    std::unique_ptr<Save_physics_input> reloaded_input;
    std::filesystem::path               first_path;
    std::filesystem::path               second_path;
    erhe::usd::Usd_load_result          source;
    erhe::usd::Usd_load_result          reloaded;
    erhe::usd::Usd_save_result          first_save;
    erhe::usd::Usd_save_result          second_save;
};

[[nodiscard]] auto body_at(const erhe::usd::Usd_data& data, const std::string& stage_path) -> const erhe::scene::Physics_node_description*
{
    for (std::size_t index = 0, end = data.physics_prims.bodies.size(); index < end; ++index) {
        if (data.physics_prims.bodies[index].stage_path == stage_path) {
            return &data.physics.node_physics[index];
        }
    }
    return nullptr;
}

[[nodiscard]] auto shape_of(
    const erhe::usd::Usd_data&                   data,
    const erhe::scene::Physics_node_description* body
) -> const erhe::scene::Physics_shape*
{
    if ((body == nullptr) || !body->collider.has_value() || !body->collider.value().geometry.shape_index.has_value()) {
        return nullptr;
    }
    return &data.physics.shapes[body->collider.value().geometry.shape_index.value()];
}

[[nodiscard]] auto joint_named(const erhe::usd::Usd_data& data, const std::string& name) -> const erhe::scene::Physics_joint_description*
{
    for (const erhe::scene::Physics_joint_description& joint : data.physics.joints) {
        if (joint.name == name) {
            return &joint;
        }
    }
    return nullptr;
}

class Physics_export : public testing::Test
{
protected:
    void SetUp() override
    {
        trip = std::make_unique<Physics_round_trip>();
        ASSERT_TRUE(trip->source.error.empty()) << trip->source.error;
        ASSERT_TRUE(trip->first_save.error.empty()) << trip->first_save.error;
        ASSERT_TRUE(trip->reloaded.error.empty()) << trip->reloaded.error;
        ASSERT_TRUE(trip->second_save.error.empty()) << trip->second_save.error;
    }

    std::unique_ptr<Physics_round_trip> trip;
};

TEST_F(Physics_export, the_second_write_is_the_first)
{
    const std::string first  = read_file(trip->first_path);
    const std::string second = read_file(trip->second_path);
    EXPECT_FALSE(first.empty());
    EXPECT_EQ(first, second);
}

TEST_F(Physics_export, the_physics_scene_survives)
{
    const erhe::usd::Usd_physics_scene& scene = trip->reloaded.data.physics_prims.scene;
    ASSERT_TRUE(scene.present);
    ASSERT_TRUE(scene.gravity_direction.has_value());
    EXPECT_NEAR(scene.gravity_direction.value().y, -1.0f, c_tolerance);
    ASSERT_TRUE(scene.gravity_magnitude.has_value());
    EXPECT_NEAR(scene.gravity_magnitude.value(), 9.81f, c_tolerance);
}

TEST_F(Physics_export, the_physics_material_survives)
{
    ASSERT_EQ(trip->reloaded.data.physics.materials.size(), 1u);
    const erhe::scene::Physics_material_description& material = trip->reloaded.data.physics.materials.front();
    EXPECT_EQ(material.name, "Rubber");
    EXPECT_NEAR(material.static_friction,  0.9f, c_tolerance);
    EXPECT_NEAR(material.dynamic_friction, 0.8f, c_tolerance);
    EXPECT_NEAR(material.restitution,      0.4f, c_tolerance);
    EXPECT_EQ(material.friction_combine,    erhe::scene::Physics_combine_mode::e_maximum);
    EXPECT_EQ(material.restitution_combine, erhe::scene::Physics_combine_mode::e_minimum);

    const erhe::usd::Usd_physics_record& record = trip->reloaded.data.physics_prims.materials.front();
    EXPECT_EQ(record.stage_path, "/World/Looks/Rubber");
    bool has_density = false;
    bool has_damping = false;
    for (const erhe::usd::Usd_physics_property& property : record.properties) {
        if (property.name == "Physics_material.density")        { has_density = true; EXPECT_EQ(property.value, "1100"); }
        if (property.name == "Physics_material.linear_damping")  { has_damping = true; EXPECT_EQ(property.value, "0.25"); }
    }
    EXPECT_TRUE(has_density);
    EXPECT_TRUE(has_damping);
}

TEST_F(Physics_export, the_collision_filters_survive)
{
    ASSERT_EQ(trip->reloaded.data.physics.collision_filters.size(), 2u);
    for (const erhe::scene::Physics_collision_filter_description& filter : trip->reloaded.data.physics.collision_filters) {
        if (filter.name == "Props") {
            ASSERT_EQ(filter.collision_systems.size(), 1u);
            EXPECT_EQ(filter.collision_systems.front(), "Props");
            ASSERT_EQ(filter.not_collide_with_systems.size(), 1u);
            EXPECT_EQ(filter.not_collide_with_systems.front(), "Characters");
        } else {
            EXPECT_EQ(filter.name, "Characters");
            ASSERT_EQ(filter.collision_systems.size(), 1u);
            EXPECT_EQ(filter.collision_systems.front(), "characters");
            ASSERT_EQ(filter.collide_with_systems.size(), 2u);
            EXPECT_EQ(filter.collide_with_systems[0], "props");
            EXPECT_EQ(filter.collide_with_systems[1], "terrain");
        }
    }
}

TEST_F(Physics_export, the_bodies_and_their_shapes_survive)
{
    const erhe::scene::Physics_node_description* crate = body_at(trip->reloaded.data, "/World/Crate");
    ASSERT_NE(crate, nullptr);
    ASSERT_TRUE(crate->motion.has_value());
    const erhe::scene::Physics_node_motion& motion = crate->motion.value();
    EXPECT_FALSE(motion.is_kinematic);
    ASSERT_TRUE(motion.mass.has_value());
    EXPECT_NEAR(motion.mass.value(),       12.0f,   c_tolerance);
    EXPECT_NEAR(motion.center_of_mass.y,   0.25f,   c_tolerance);
    EXPECT_NEAR(motion.linear_velocity.x,  1.0f,    c_tolerance);
    EXPECT_NEAR(motion.angular_velocity.y, 1.5708f, c_tolerance);

    const erhe::scene::Physics_node_description* crate_collider = body_at(trip->reloaded.data, "/World/Crate/collider");
    const erhe::scene::Physics_shape* const      box            = shape_of(trip->reloaded.data, crate_collider);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->type, erhe::scene::Physics_shape_type::e_box);
    EXPECT_NEAR(box->size.x, 2.0f, c_tolerance);
    ASSERT_TRUE(crate_collider->collider.value().material_index.has_value());
    EXPECT_EQ(
        trip->reloaded.data.physics.materials[crate_collider->collider.value().material_index.value()].name,
        "Rubber"
    );
    ASSERT_TRUE(crate_collider->collider.value().filter_index.has_value());
    EXPECT_EQ(
        trip->reloaded.data.physics.collision_filters[crate_collider->collider.value().filter_index.value()].name,
        "Props"
    );

    const erhe::scene::Physics_node_description* ball = body_at(trip->reloaded.data, "/World/Ball");
    ASSERT_NE(ball, nullptr);
    ASSERT_TRUE(ball->motion.has_value());
    EXPECT_TRUE(ball->motion.value().is_kinematic);
    const erhe::scene::Physics_shape* const sphere = shape_of(trip->reloaded.data, body_at(trip->reloaded.data, "/World/Ball/collider"));
    ASSERT_NE(sphere, nullptr);
    EXPECT_EQ(sphere->type, erhe::scene::Physics_shape_type::e_sphere);
    EXPECT_NEAR(sphere->radius, 0.75f, c_tolerance);

}

// A box of unequal extents is a unit `Cube` scaled per axis, so the size the
// fold saw comes back as the shape's dimensions times the prim's own scale -
// which is what the physics import composes again.
TEST_F(Physics_export, a_scaled_box_collider_survives)
{
    const erhe::scene::Physics_node_description* const collider_prim =
        body_at(trip->reloaded.data, "/World/Slab/collider");
    const erhe::scene::Physics_shape* const box = shape_of(trip->reloaded.data, collider_prim);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->type, erhe::scene::Physics_shape_type::e_box);
    EXPECT_NEAR(box->size.x, 1.0f, c_tolerance);
    EXPECT_NEAR(box->size.y, 1.0f, c_tolerance);
    EXPECT_NEAR(box->size.z, 1.0f, c_tolerance);

    ASSERT_TRUE(collider_prim->node.operator bool());
    const glm::vec3 scale = collider_prim->node->parent_from_node_transform().get_scale();
    EXPECT_NEAR(scale.x, 2.0f, c_tolerance);
    EXPECT_NEAR(scale.y, 0.5f, c_tolerance);
    EXPECT_NEAR(scale.z, 3.0f, c_tolerance);
}

TEST_F(Physics_export, the_trigger_body_survives)
{
    const erhe::scene::Physics_node_description* sensor = body_at(trip->reloaded.data, "/World/Sensor");
    ASSERT_NE(sensor, nullptr);
    EXPECT_TRUE(sensor->trigger.has_value());
    EXPECT_FALSE(sensor->collider.has_value());
    ASSERT_TRUE(sensor->motion.has_value());
    EXPECT_NEAR(sensor->motion.value().gravity_factor, 0.5f, c_tolerance);

    const erhe::scene::Physics_node_description* collider_prim = body_at(trip->reloaded.data, "/World/Sensor/collider");
    ASSERT_NE(collider_prim, nullptr);
    const erhe::scene::Physics_shape* const capsule = shape_of(trip->reloaded.data, collider_prim);
    ASSERT_NE(capsule, nullptr);
    EXPECT_EQ(capsule->type, erhe::scene::Physics_shape_type::e_capsule);
    EXPECT_NEAR(capsule->height,        1.5f, c_tolerance);
    EXPECT_NEAR(capsule->radius_bottom, 0.6f, c_tolerance);
    EXPECT_NEAR(capsule->radius_top,    0.3f, c_tolerance);
}

TEST_F(Physics_export, the_mesh_colliders_survive)
{
    const erhe::scene::Physics_node_description* ground = body_at(trip->reloaded.data, "/World/Ground");
    ASSERT_NE(ground, nullptr);
    EXPECT_FALSE(ground->motion.has_value());
    ASSERT_TRUE(ground->collider.has_value());
    EXPECT_TRUE(ground->collider.value().geometry.mesh.operator bool());
    EXPECT_FALSE(ground->collider.value().geometry.convex_hull);

    // A body whose shape was built from a mesh prim below it states the
    // collider on that prim: the collision schemas are on `/World/Rock/shell`,
    // where a reload finds the mesh, and the body prim above states its
    // motion alone.
    const erhe::scene::Physics_node_description* shell = body_at(trip->reloaded.data, "/World/Rock/shell");
    ASSERT_NE(shell, nullptr);
    ASSERT_TRUE(shell->collider.has_value());
    ASSERT_TRUE(shell->collider.value().geometry.mesh.operator bool());
    EXPECT_EQ(shell->collider.value().geometry.mesh->get_name(), "shell");
    EXPECT_TRUE(shell->collider.value().geometry.convex_hull);

    const erhe::scene::Physics_node_description* rock = body_at(trip->reloaded.data, "/World/Rock");
    ASSERT_NE(rock, nullptr);
    EXPECT_TRUE(rock->motion.has_value());
    EXPECT_FALSE(rock->collider.has_value());

    const std::string  first      = read_file(trip->first_path);
    const std::size_t  rock_at    = first.find("def Xform \"Rock\"");
    const std::size_t  shell_at   = first.find("def Mesh \"shell\"");
    ASSERT_NE(rock_at,  std::string::npos);
    ASSERT_NE(shell_at, std::string::npos);
    EXPECT_LT(rock_at, shell_at);
    const std::string rock_prim = first.substr(rock_at, shell_at - rock_at);
    EXPECT_EQ(rock_prim.find("PhysicsCollisionAPI"),  std::string::npos);
    EXPECT_EQ(rock_prim.find("physics:approximation"), std::string::npos);
    const std::string shell_prim = first.substr(shell_at);
    EXPECT_NE(shell_prim.find("PhysicsMeshCollisionAPI"), std::string::npos);
    EXPECT_NE(shell_prim.find("token physics:approximation = \"convexHull\""), std::string::npos);
}

TEST_F(Physics_export, the_shared_joint_settings_survive)
{
    const erhe::scene::Physics_joint_description* settings = joint_named(trip->reloaded.data, "Hinge_settings");
    ASSERT_NE(settings, nullptr);
    ASSERT_EQ(settings->limits.size(), 1u);
    const erhe::scene::Physics_joint_limit& limit = settings->limits.front();
    EXPECT_TRUE(limit.linear_axes.empty());
    ASSERT_EQ(limit.angular_axes.size(), 2u);
    EXPECT_EQ(limit.angular_axes[0], 0);
    EXPECT_EQ(limit.angular_axes[1], 1);
    ASSERT_TRUE(limit.min.has_value());
    EXPECT_NEAR(limit.min.value(), -0.785398f, c_tolerance);
    EXPECT_NEAR(limit.max.value(),  0.785398f, c_tolerance);
    ASSERT_TRUE(limit.stiffness.has_value());
    EXPECT_NEAR(limit.stiffness.value(), 120.0f, c_tolerance);
    EXPECT_NEAR(limit.damping, 3.0f, c_tolerance);

    ASSERT_EQ(settings->drives.size(), 1u);
    const erhe::scene::Physics_joint_drive& drive = settings->drives.front();
    EXPECT_EQ(drive.type, erhe::scene::Physics_drive_type::e_linear);
    EXPECT_EQ(drive.mode, erhe::scene::Physics_drive_mode::e_acceleration);
    EXPECT_EQ(drive.axis, 2);
    EXPECT_NEAR(drive.max_force,       500.0f, c_tolerance);
    EXPECT_NEAR(drive.position_target, 0.25f,  c_tolerance);
    EXPECT_NEAR(drive.velocity_target, 1.5f,   c_tolerance);
    EXPECT_NEAR(drive.stiffness,       80.0f,  c_tolerance);
    EXPECT_NEAR(drive.damping,         4.0f,   c_tolerance);

    // The settings item is a prim of its own, and the joint using it names it.
    bool found_settings_prim = false;
    for (const erhe::usd::Usd_physics_record& record : trip->reloaded.data.physics_prims.joint_settings) {
        if (record.stage_path == "/World/Joints/Hinge_settings") {
            found_settings_prim = true;
        }
    }
    EXPECT_TRUE(found_settings_prim);
}

TEST_F(Physics_export, the_joints_survive)
{
    const erhe::scene::Physics_node_description* crate = body_at(trip->reloaded.data, "/World/Crate");
    ASSERT_NE(crate, nullptr);
    ASSERT_TRUE(crate->joint.has_value());
    ASSERT_TRUE(crate->joint.value().connected_node.operator bool());
    EXPECT_EQ(crate->joint.value().connected_node->get_name(), "Anchor");
    EXPECT_TRUE(crate->joint.value().enable_collision);
    EXPECT_EQ(
        trip->reloaded.data.physics.joints[crate->joint.value().joint_index].name,
        "Hinge_settings"
    );

    // A revolute joint is a six-dof joint with one rotational limit, and a
    // fixed joint one limit at zero over all six axes; both are written back
    // as the instances they read as.
    const erhe::scene::Physics_node_description* ball = body_at(trip->reloaded.data, "/World/Ball");
    ASSERT_NE(ball, nullptr);
    ASSERT_TRUE(ball->joint.has_value());
    const erhe::scene::Physics_joint_description& door = trip->reloaded.data.physics.joints[ball->joint.value().joint_index];
    ASSERT_EQ(door.limits.size(), 1u);
    ASSERT_EQ(door.limits.front().angular_axes.size(), 1u);
    EXPECT_EQ(door.limits.front().angular_axes.front(), 1);
    ASSERT_TRUE(door.limits.front().min.has_value());
    EXPECT_NEAR(door.limits.front().min.value(), -1.5708f, c_tolerance);
    EXPECT_NEAR(door.limits.front().max.value(),  1.5708f, c_tolerance);

    const erhe::scene::Physics_node_description* rock = body_at(trip->reloaded.data, "/World/Rock");
    ASSERT_NE(rock, nullptr);
    ASSERT_TRUE(rock->joint.has_value());
    const erhe::scene::Physics_joint_description& weld = trip->reloaded.data.physics.joints[rock->joint.value().joint_index];
    ASSERT_EQ(weld.limits.size(), 1u);
    EXPECT_EQ(weld.limits.front().linear_axes.size(),  3u);
    EXPECT_EQ(weld.limits.front().angular_axes.size(), 3u);
    ASSERT_TRUE(weld.limits.front().min.has_value());
    EXPECT_NEAR(weld.limits.front().min.value(), 0.0f, c_tolerance);
    EXPECT_NEAR(weld.limits.front().max.value(), 0.0f, c_tolerance);
}

// A joint sitting on a frame node below its body is written as the body it
// hangs below plus that node's transform as the joint frame, and the frame
// node is written as the `Xform` prim it is - so the reload finds that node
// again rather than making a second one
// (doc/usd_compatibility.md, "Physics").
TEST_F(Physics_export, the_joint_frames_survive)
{
    const std::string first = read_file(trip->first_path);
    EXPECT_NE(first.find("rel physics:body0 = </World/Panel>"), std::string::npos);
    EXPECT_NE(first.find("rel physics:body1 = </World/Post>"),  std::string::npos);
    EXPECT_NE(first.find("physics:localPos0 = (0.5, 0, 0)"),    std::string::npos);
    EXPECT_NE(first.find("physics:localPos1 = (0.5, 0, 2)"),    std::string::npos);

    const erhe::scene::Physics_node_description* frame0 = body_at(trip->reloaded.data, "/World/Panel/Flap_frame0");
    ASSERT_NE(frame0, nullptr);
    ASSERT_TRUE(frame0->joint.has_value());
    ASSERT_TRUE(frame0->node.operator bool());
    const erhe::scene::Trs_transform& frame0_transform = frame0->node->parent_from_node_transform();
    EXPECT_NEAR(frame0_transform.get_translation().x, 0.5f, c_tolerance);
    EXPECT_NEAR(std::abs(frame0_transform.get_rotation().w), 0.7071068f, c_tolerance);
    EXPECT_NEAR(std::abs(frame0_transform.get_rotation().x), 0.7071068f, c_tolerance);

    const std::shared_ptr<erhe::scene::Node>& connected = frame0->joint.value().connected_node;
    ASSERT_TRUE(connected.operator bool());
    EXPECT_EQ(connected->get_name(), "Flap_frame1");

    // One frame node per side, and no second one beside it.
    std::size_t frame_children = 0;
    for (const std::shared_ptr<erhe::Hierarchy>& child : frame0->node->get_parent_node()->get_children()) {
        if (child->get_name().find("Flap_frame0") != std::string::npos) {
            ++frame_children;
        }
    }
    EXPECT_EQ(frame_children, 1u);
}

} // anonymous namespace
