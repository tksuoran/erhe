#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_physics/transform.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <memory>
#include <vector>

// Box3D has no activation listener and reports sensor touches per SHAPE pair,
// so Box3d_world synthesizes both erhe-level event streams. These tests step a
// real world and assert on the resulting edges.

namespace {

using erhe::physics::ICollision_shape;
using erhe::physics::IRigid_body;
using erhe::physics::IRigid_body_create_info;
using erhe::physics::IWorld;
using erhe::physics::Motion_mode;
using erhe::physics::Transform;
using erhe::physics::Trigger_event;

constexpr double step_dt = 1.0 / 60.0;

class Recorded_event
{
public:
    IRigid_body* sensor  {nullptr};
    IRigid_body* other   {nullptr};
    bool         is_enter{false};
};

// A visitor made of two boxes: Box3D reports one sensor touch per child shape,
// so this is what the body-pair deduplication has to collapse.
[[nodiscard]] auto make_two_child_compound() -> std::shared_ptr<ICollision_shape>
{
    erhe::physics::Compound_shape_create_info create_info;
    for (int i = 0; i < 2; ++i) {
        erhe::physics::Compound_child child;
        child.shape     = ICollision_shape::create_box_shape_shared(glm::vec3{0.4f, 0.4f, 0.4f});
        child.transform = Transform{glm::mat3{1.0f}, glm::vec3{(i == 0) ? -0.4f : 0.4f, 0.0f, 0.0f}};
        create_info.children.push_back(child);
    }
    return ICollision_shape::create_compound_shape_shared(create_info);
}

} // anonymous namespace

TEST(world_events, a_compound_visitor_produces_one_enter_and_one_exit_per_body_pair)
{
    const std::unique_ptr<IWorld> world = IWorld::create_unique();

    std::vector<Recorded_event> events;
    world->set_on_trigger_enter([&events](const Trigger_event& event) {
        events.push_back(Recorded_event{.sensor = event.sensor, .other = event.other, .is_enter = true});
    });
    world->set_on_trigger_exit([&events](const Trigger_event& event) {
        events.push_back(Recorded_event{.sensor = event.sensor, .other = event.other, .is_enter = false});
    });

    IRigid_body_create_info sensor_create_info;
    sensor_create_info.collision_shape = ICollision_shape::create_box_shape_shared(glm::vec3{2.0f, 0.2f, 2.0f});
    sensor_create_info.motion_mode     = Motion_mode::e_static;
    sensor_create_info.is_sensor       = true;
    sensor_create_info.position        = glm::vec3{0.0f, 0.0f, 0.0f};
    sensor_create_info.debug_label     = "sensor";
    const std::shared_ptr<IRigid_body> sensor = world->create_rigid_body_shared(sensor_create_info);

    // Thrown down from above the sensor, falls through it and out the other
    // side. A body at rest enters the world asleep, so the initial velocity is
    // what sets it in motion.
    IRigid_body_create_info visitor_create_info;
    visitor_create_info.collision_shape = make_two_child_compound();
    visitor_create_info.motion_mode     = Motion_mode::e_dynamic;
    visitor_create_info.position        = glm::vec3{0.0f, 3.0f, 0.0f};
    visitor_create_info.linear_velocity = glm::vec3{0.0f, -1.0f, 0.0f};
    visitor_create_info.debug_label     = "visitor";
    const std::shared_ptr<IRigid_body> visitor = world->create_rigid_body_shared(visitor_create_info);

    world->add_rigid_body(sensor.get());
    world->add_rigid_body(visitor.get());

    for (int i = 0; i < 240; ++i) {
        world->update_fixed_step(step_dt);
    }

    ASSERT_EQ(events.size(), 2u);
    EXPECT_TRUE (events[0].is_enter);
    EXPECT_FALSE(events[1].is_enter);
    for (const Recorded_event& event : events) {
        EXPECT_EQ(event.sensor, sensor.get());
        EXPECT_EQ(event.other,  visitor.get());
    }

    world->remove_rigid_body(visitor.get());
    world->remove_rigid_body(sensor.get());
}

TEST(world_events, removing_a_body_mid_overlap_emits_the_exit)
{
    const std::unique_ptr<IWorld> world = IWorld::create_unique();

    std::vector<Recorded_event> events;
    world->set_on_trigger_enter([&events](const Trigger_event& event) {
        events.push_back(Recorded_event{.sensor = event.sensor, .other = event.other, .is_enter = true});
    });
    world->set_on_trigger_exit([&events](const Trigger_event& event) {
        events.push_back(Recorded_event{.sensor = event.sensor, .other = event.other, .is_enter = false});
    });

    IRigid_body_create_info sensor_create_info;
    sensor_create_info.collision_shape = ICollision_shape::create_box_shape_shared(glm::vec3{2.0f, 2.0f, 2.0f});
    sensor_create_info.motion_mode     = Motion_mode::e_static;
    sensor_create_info.is_sensor       = true;
    sensor_create_info.debug_label     = "sensor";
    const std::shared_ptr<IRigid_body> sensor = world->create_rigid_body_shared(sensor_create_info);

    // Weightless so it stays inside the sensor volume for the whole run.
    IRigid_body_create_info visitor_create_info;
    visitor_create_info.collision_shape = ICollision_shape::create_sphere_shape_shared(0.5f);
    visitor_create_info.motion_mode     = Motion_mode::e_dynamic;
    visitor_create_info.gravity_factor  = 0.0f;
    visitor_create_info.debug_label     = "visitor";
    const std::shared_ptr<IRigid_body> visitor = world->create_rigid_body_shared(visitor_create_info);

    world->add_rigid_body(sensor.get());
    world->add_rigid_body(visitor.get());

    for (int i = 0; i < 10; ++i) {
        world->update_fixed_step(step_dt);
    }
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].is_enter);

    // Box3D would only report the end touch on the NEXT step, when the wrapper
    // may already be gone; the world emits it from remove_rigid_body instead.
    world->remove_rigid_body(visitor.get());
    ASSERT_EQ(events.size(), 2u);
    EXPECT_FALSE(events[1].is_enter);
    EXPECT_EQ(events[1].sensor, sensor.get());
    EXPECT_EQ(events[1].other,  visitor.get());

    // And the late end touch must not produce a second exit.
    for (int i = 0; i < 10; ++i) {
        world->update_fixed_step(step_dt);
    }
    EXPECT_EQ(events.size(), 2u);

    world->remove_rigid_body(sensor.get());
}

TEST(world_events, a_falling_body_activates_then_deactivates_when_it_sleeps)
{
    const std::unique_ptr<IWorld> world = IWorld::create_unique();

    std::vector<IRigid_body*> activated;
    std::vector<IRigid_body*> deactivated;
    world->set_on_body_activated  ([&activated  ](IRigid_body* body) { activated  .push_back(body); });
    world->set_on_body_deactivated([&deactivated](IRigid_body* body) { deactivated.push_back(body); });

    IRigid_body_create_info ground_create_info;
    ground_create_info.collision_shape = ICollision_shape::create_box_shape_shared(glm::vec3{10.0f, 0.5f, 10.0f});
    ground_create_info.motion_mode     = Motion_mode::e_static;
    ground_create_info.position        = glm::vec3{0.0f, -0.5f, 0.0f};
    ground_create_info.debug_label     = "ground";
    const std::shared_ptr<IRigid_body> ground = world->create_rigid_body_shared(ground_create_info);

    IRigid_body_create_info box_create_info;
    box_create_info.collision_shape = ICollision_shape::create_box_shape_shared(glm::vec3{0.5f, 0.5f, 0.5f});
    box_create_info.motion_mode     = Motion_mode::e_dynamic;
    box_create_info.position        = glm::vec3{0.0f, 2.0f, 0.0f};
    box_create_info.linear_velocity = glm::vec3{0.0f, -1.0f, 0.0f}; // a body at rest enters the world asleep
    box_create_info.debug_label     = "box";
    const std::shared_ptr<IRigid_body> box = world->create_rigid_body_shared(box_create_info);

    world->add_rigid_body(ground.get());
    world->add_rigid_body(box.get());

    // Long enough to fall, settle and reach Box3D's sleep threshold.
    for (int i = 0; i < 600; ++i) {
        world->update_fixed_step(step_dt);
    }

    // Exactly one edge each way: the falling box, and the box going to sleep.
    // The static ground never moves, so it never appears in the move stream.
    ASSERT_EQ(activated.size(), 1u);
    EXPECT_EQ(activated[0], box.get());
    ASSERT_EQ(deactivated.size(), 1u);
    EXPECT_EQ(deactivated[0], box.get());
    EXPECT_FALSE(box->is_active());

    world->remove_rigid_body(box.get());
    world->remove_rigid_body(ground.get());
}
