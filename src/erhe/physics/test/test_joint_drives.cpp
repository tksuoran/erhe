// Six-DOF joints stepped through a real IWorld on whichever backend the
// library is built with: the fold of a fixed non-zero translation, a distance
// range with free rotation, and the two drive kinds in acceleration mode.
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/iconstraint.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/iworld.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

namespace {

using erhe::physics::Constraint_axis_drive;
using erhe::physics::Constraint_axis_limit;
using erhe::physics::Drive_force_mode;
using erhe::physics::Six_dof_constraint_settings;
using erhe::physics::Transform;

constexpr float c_fixed_step = 1.0f / 60.0f;

[[nodiscard]] auto make_box(
    erhe::physics::IWorld&          world,
    const char*                     debug_label,
    const erhe::physics::Motion_mode motion_mode,
    const glm::vec3                 position
) -> std::shared_ptr<erhe::physics::IRigid_body>
{
    erhe::physics::IRigid_body_create_info create_info{};
    create_info.collision_shape = erhe::physics::ICollision_shape::create_box_shape_shared(glm::vec3{0.1f, 0.1f, 0.1f});
    create_info.debug_label     = debug_label;
    create_info.motion_mode     = motion_mode;
    create_info.mass            = (motion_mode == erhe::physics::Motion_mode::e_dynamic) ? 1.0f : 0.0f;
    create_info.position        = position;
    create_info.gravity_factor  = 1.0f;
    // A body at rest enters the world asleep (IWorld::add_rigid_body), and a
    // joint of sleeping bodies is not solved: the smallest velocity makes the
    // body enter awake.
    if (motion_mode == erhe::physics::Motion_mode::e_dynamic) {
        create_info.linear_velocity = glm::vec3{0.0f, -1.0e-4f, 0.0f};
    }
    return world.create_rigid_body_shared(create_info);
}

[[nodiscard]] auto fixed_axis() -> Constraint_axis_limit
{
    return Constraint_axis_limit{.limited = true, .min = 0.0f, .max = 0.0f};
}

[[nodiscard]] auto fixed_at(const float value) -> Constraint_axis_limit
{
    return Constraint_axis_limit{.limited = true, .min = value, .max = value};
}

[[nodiscard]] auto ranged_axis(const float min, const float max) -> Constraint_axis_limit
{
    return Constraint_axis_limit{.limited = true, .min = min, .max = max};
}

// A hinge about X between a static anchor at the origin and a dynamic box at
// (1, 0, 0): frames at both body origins, translation X fixed at 1 (folded
// into frame A by the backend), rotation X free.
class Hinge
{
public:
    explicit Hinge(erhe::physics::IWorld& world)
    {
        anchor = make_box(world, "anchor", erhe::physics::Motion_mode::e_static,  glm::vec3{0.0f, 0.0f, 0.0f});
        body   = make_box(world, "body",   erhe::physics::Motion_mode::e_dynamic, glm::vec3{1.0f, 0.0f, 0.0f});
        world.add_rigid_body(anchor.get());
        world.add_rigid_body(body.get());
        settings.rigid_body_a = anchor.get();
        settings.rigid_body_b = body.get();
        settings.frame_in_a   = Transform{};
        settings.frame_in_b   = Transform{};
        settings.limits       = {fixed_at(1.0f), fixed_axis(), fixed_axis(), Constraint_axis_limit{}, fixed_axis(), fixed_axis()};
    }

    void build(erhe::physics::IWorld& world)
    {
        constraint = erhe::physics::IConstraint::create_six_dof_constraint_shared(settings);
        world.add_constraint(constraint.get());
    }

    void tear_down(erhe::physics::IWorld& world)
    {
        world.remove_constraint(constraint.get());
        constraint.reset();
        world.remove_rigid_body(body.get());
        world.remove_rigid_body(anchor.get());
    }

    // The hinge angle: the rotation of the body's Y axis about X.
    [[nodiscard]] auto angle() const -> float
    {
        const glm::mat4 m = body->get_world_transform();
        return std::atan2(m[1][2], m[1][1]);
    }

    std::shared_ptr<erhe::physics::IRigid_body> anchor;
    std::shared_ptr<erhe::physics::IRigid_body> body;
    std::shared_ptr<erhe::physics::IConstraint> constraint;
    Six_dof_constraint_settings                 settings{};
};

} // anonymous namespace

TEST(Joint_drives, fixed_translation_offset_is_held)
{
    const std::unique_ptr<erhe::physics::IWorld> world = erhe::physics::IWorld::create_unique();
    world->set_gravity(glm::vec3{0.0f, -9.81f, 0.0f});
    Hinge hinge{*world};
    hinge.build(*world);
    for (int i = 0; i < 120; ++i) {
        world->update_fixed_step(c_fixed_step);
    }
    // The box hangs on the hinge axis through its own origin: it stays put
    // instead of being pulled to the anchor.
    const glm::vec3 position = glm::vec3{hinge.body->get_world_transform()[3]};
    EXPECT_NEAR(1.0f, position.x, 0.01f);
    EXPECT_NEAR(0.0f, position.y, 0.02f);
    EXPECT_NEAR(0.0f, position.z, 0.01f);
    hinge.tear_down(*world);
}

TEST(Joint_drives, acceleration_mode_velocity_drive_reaches_its_target)
{
    const std::unique_ptr<erhe::physics::IWorld> world = erhe::physics::IWorld::create_unique();
    world->set_gravity(glm::vec3{0.0f, 0.0f, 0.0f});
    Hinge hinge{*world};
    Constraint_axis_drive& drive = hinge.settings.drives[3];
    drive.enabled             = true;
    drive.mode                = Drive_force_mode::acceleration;
    drive.use_position_target = false;
    drive.velocity_target     = 1.5f;
    drive.damping             = 4.0f; // 1/s: the error decays in a quarter second
    hinge.build(*world);
    for (int i = 0; i < 120; ++i) {
        world->update_fixed_step(c_fixed_step);
    }
    EXPECT_TRUE(hinge.body->is_active());
    EXPECT_NEAR(1.5f, hinge.body->get_angular_velocity().x, 0.1f);
    hinge.tear_down(*world);
}

TEST(Joint_drives, velocity_drive_turns_a_sphere_hanging_under_gravity)
{
    // The RigidBodies_Joint_09 test asset: a unit sphere on a hinge through
    // its own centre, driven at pi / 2 rad/s with unit damping.
    const std::unique_ptr<erhe::physics::IWorld> world = erhe::physics::IWorld::create_unique();
    world->set_gravity(glm::vec3{0.0f, -9.81f, 0.0f});
    Hinge hinge{*world};
    // The anchor is a unit sphere as well, so the two touch at the hinge
    // axis: the joint excludes their collision.
    {
        erhe::physics::IRigid_body_create_info anchor_info{};
        anchor_info.collision_shape = erhe::physics::ICollision_shape::create_sphere_shape_shared(0.5f);
        anchor_info.debug_label     = "anchor sphere";
        anchor_info.motion_mode     = erhe::physics::Motion_mode::e_static;
        anchor_info.mass            = 0.0f;
        world->remove_rigid_body(hinge.anchor.get());
        hinge.anchor = world->create_rigid_body_shared(anchor_info);
        world->add_rigid_body(hinge.anchor.get());
        hinge.settings.rigid_body_a = hinge.anchor.get();
    }
    erhe::physics::IRigid_body_create_info create_info{};
    create_info.collision_shape = erhe::physics::ICollision_shape::create_sphere_shape_shared(0.5f);
    create_info.debug_label     = "sphere";
    create_info.motion_mode     = erhe::physics::Motion_mode::e_dynamic;
    create_info.mass            = 1.0f;
    create_info.position        = glm::vec3{1.0f, 0.0f, 0.0f};
    create_info.linear_velocity = glm::vec3{0.0f, -1.0e-4f, 0.0f};
    world->remove_rigid_body(hinge.body.get());
    hinge.body = world->create_rigid_body_shared(create_info);
    world->add_rigid_body(hinge.body.get());
    hinge.settings.rigid_body_b = hinge.body.get();
    Constraint_axis_drive& drive = hinge.settings.drives[3];
    drive.enabled             = true;
    drive.mode                = Drive_force_mode::acceleration;
    drive.use_position_target = false;
    drive.velocity_target     = glm::half_pi<float>();
    drive.damping             = 1.0f;
    hinge.build(*world);
    for (int i = 0; i < 240; ++i) {
        // The editor writes every body's node pose back into the body before
        // each step (Node_physics_system::before_physics_simulation), a round
        // trip through the node's transform that must not disturb the motion.
        for (const std::shared_ptr<erhe::physics::IRigid_body>& body : {hinge.anchor, hinge.body}) {
            const glm::mat4 m = body->get_world_transform();
            body->set_world_transform(Transform{glm::mat3{m}, glm::vec3{m[3]}});
        }
        world->update_fixed_step(1.0f / 240.0f);
    }
    // One second at a one second time constant: about 63% of the way.
    EXPECT_GT(hinge.body->get_angular_velocity().x, 0.7f);
    EXPECT_LT(hinge.body->get_angular_velocity().x, 1.3f);
    hinge.tear_down(*world);
}

TEST(Joint_drives, velocity_drive_gain_bounds_the_force)
{
    // With a small gain the drive is a weak viscous coupling: after a short
    // time the body has only started to turn. A hard motor would be at the
    // target within a step.
    const std::unique_ptr<erhe::physics::IWorld> world = erhe::physics::IWorld::create_unique();
    world->set_gravity(glm::vec3{0.0f, 0.0f, 0.0f});
    Hinge hinge{*world};
    Constraint_axis_drive& drive = hinge.settings.drives[3];
    drive.enabled             = true;
    drive.mode                = Drive_force_mode::acceleration;
    drive.use_position_target = false;
    drive.velocity_target     = 1.5f;
    drive.damping             = 0.5f; // 1/s: a two second time constant
    hinge.build(*world);
    for (int i = 0; i < 6; ++i) {
        world->update_fixed_step(c_fixed_step); // 0.1 s: about 5% of the way
    }
    const float speed = hinge.body->get_angular_velocity().x;
    EXPECT_GT(speed, 0.02f);
    EXPECT_LT(speed, 0.3f);
    hinge.tear_down(*world);
}

TEST(Joint_drives, acceleration_mode_position_drive_reaches_its_target)
{
    const std::unique_ptr<erhe::physics::IWorld> world = erhe::physics::IWorld::create_unique();
    world->set_gravity(glm::vec3{0.0f, 0.0f, 0.0f});
    Hinge hinge{*world};
    Constraint_axis_drive& drive = hinge.settings.drives[3];
    drive.enabled             = true;
    drive.mode                = Drive_force_mode::acceleration;
    drive.use_position_target = true;
    drive.position_target     = 0.5f;
    drive.stiffness           = 100.0f; // 1/s^2: a 1.6 Hz spring whatever the inertia
    drive.damping             = 20.0f;  // 1/s: critically damped
    hinge.build(*world);
    for (int i = 0; i < 180; ++i) {
        world->update_fixed_step(c_fixed_step);
    }
    EXPECT_NEAR(0.5f, hinge.angle(), 0.05f);
    hinge.tear_down(*world);
}

TEST(Joint_drives, distance_range_tethers_a_falling_body)
{
    const std::unique_ptr<erhe::physics::IWorld> world = erhe::physics::IWorld::create_unique();
    world->set_gravity(glm::vec3{0.0f, -9.81f, 0.0f});
    const std::shared_ptr<erhe::physics::IRigid_body> anchor = make_box(*world, "anchor", erhe::physics::Motion_mode::e_static,  glm::vec3{0.0f, 0.0f, 0.0f});
    const std::shared_ptr<erhe::physics::IRigid_body> body   = make_box(*world, "body",   erhe::physics::Motion_mode::e_dynamic, glm::vec3{0.0f, -0.5f, 0.0f});
    world->add_rigid_body(anchor.get());
    world->add_rigid_body(body.get());

    // A range of one meter on every translation axis with the rotation free:
    // a box on Jolt, the inscribed sphere (a Box3D distance joint) on Box3D;
    // straight down both end at one meter.
    Six_dof_constraint_settings settings{};
    settings.rigid_body_a = anchor.get();
    settings.rigid_body_b = body.get();
    settings.limits       = {ranged_axis(-1.0f, 1.0f), ranged_axis(-1.0f, 1.0f), ranged_axis(-1.0f, 1.0f), Constraint_axis_limit{}, Constraint_axis_limit{}, Constraint_axis_limit{}};
    const std::shared_ptr<erhe::physics::IConstraint> constraint = erhe::physics::IConstraint::create_six_dof_constraint_shared(settings);
    world->add_constraint(constraint.get());

    for (int i = 0; i < 180; ++i) {
        world->update_fixed_step(c_fixed_step);
    }
    // Fell to the end of the tether and no further.
    const glm::vec3 position = glm::vec3{body->get_world_transform()[3]};
    EXPECT_LT(position.y, -0.9f);
    EXPECT_GT(position.y, -1.05f);

    world->remove_constraint(constraint.get());
    world->remove_rigid_body(body.get());
    world->remove_rigid_body(anchor.get());
}
