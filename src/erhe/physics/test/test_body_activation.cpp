#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/iworld.hpp"

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <memory>

namespace {

constexpr float c_fixed_step = 1.0f / 60.0f;

[[nodiscard]] auto make_body(
    erhe::physics::IWorld& world,
    const char*            debug_label,
    const glm::vec3        linear_velocity,
    const glm::vec3        angular_velocity
) -> std::shared_ptr<erhe::physics::IRigid_body>
{
    erhe::physics::IRigid_body_create_info create_info{};
    create_info.collision_shape  = erhe::physics::ICollision_shape::create_box_shape_shared(glm::vec3{0.5f, 0.5f, 0.5f});
    create_info.debug_label      = debug_label;
    create_info.motion_mode      = erhe::physics::Motion_mode::e_dynamic;
    create_info.mass             = 1.0f;
    create_info.position         = glm::vec3{0.0f, 0.0f, 0.0f};
    create_info.linear_velocity  = linear_velocity;
    create_info.angular_velocity = angular_velocity;
    create_info.gravity_factor   = 0.0f; // Gravity would wake a resting body on its own
    return world.create_rigid_body_shared(create_info);
}

} // anonymous namespace

// A body authored with an initial velocity is moving, so it enters the world
// active and the first simulation step integrates that velocity.
TEST(Body_activation, moving_body_enters_world_active)
{
    const std::unique_ptr<erhe::physics::IWorld> world = erhe::physics::IWorld::create_unique();
    world->set_gravity(glm::vec3{0.0f, 0.0f, 0.0f});

    const std::shared_ptr<erhe::physics::IRigid_body> body = make_body(
        *world, "moving", glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 0.0f}
    );
    ASSERT_TRUE(body.operator bool());
    world->add_rigid_body(body.get());

    EXPECT_TRUE(body->is_active());

    world->update_fixed_step(c_fixed_step);

    EXPECT_TRUE(body->is_active());
    EXPECT_GT(body->get_world_transform()[3][0], 0.0f);
    EXPECT_GT(body->get_linear_velocity().x, 0.0f);

    world->remove_rigid_body(body.get());
}

// An angular velocity alone is enough to make the body moving.
TEST(Body_activation, spinning_body_enters_world_active)
{
    const std::unique_ptr<erhe::physics::IWorld> world = erhe::physics::IWorld::create_unique();
    world->set_gravity(glm::vec3{0.0f, 0.0f, 0.0f});

    const std::shared_ptr<erhe::physics::IRigid_body> body = make_body(
        *world, "spinning", glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}
    );
    ASSERT_TRUE(body.operator bool());
    world->add_rigid_body(body.get());

    EXPECT_TRUE(body->is_active());

    world->update_fixed_step(c_fixed_step);

    EXPECT_TRUE(body->is_active());
    EXPECT_GT(body->get_angular_velocity().y, 0.0f);

    world->remove_rigid_body(body.get());
}

// A body at rest enters the world asleep and stays there: loading a scene does
// not set its contents in motion.
TEST(Body_activation, resting_body_enters_world_asleep)
{
    const std::unique_ptr<erhe::physics::IWorld> world = erhe::physics::IWorld::create_unique();
    world->set_gravity(glm::vec3{0.0f, 0.0f, 0.0f});

    const std::shared_ptr<erhe::physics::IRigid_body> body = make_body(
        *world, "resting", glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 0.0f}
    );
    ASSERT_TRUE(body.operator bool());
    world->add_rigid_body(body.get());

    EXPECT_FALSE(body->is_active());

    world->update_fixed_step(c_fixed_step);

    EXPECT_FALSE(body->is_active());
    EXPECT_FLOAT_EQ(body->get_world_transform()[3][0], 0.0f);
    EXPECT_FLOAT_EQ(body->get_world_transform()[3][1], 0.0f);
    EXPECT_FLOAT_EQ(body->get_world_transform()[3][2], 0.0f);

    world->remove_rigid_body(body.get());
}

// Assigning a non-zero velocity to a sleeping body in the world wakes it, so
// the body never holds a velocity it is not allowed to hold.
TEST(Body_activation, setting_velocity_wakes_a_sleeping_body)
{
    const std::unique_ptr<erhe::physics::IWorld> world = erhe::physics::IWorld::create_unique();
    world->set_gravity(glm::vec3{0.0f, 0.0f, 0.0f});

    const std::shared_ptr<erhe::physics::IRigid_body> body = make_body(
        *world, "woken", glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 0.0f}
    );
    ASSERT_TRUE(body.operator bool());
    world->add_rigid_body(body.get());
    ASSERT_FALSE(body->is_active());

    body->set_linear_velocity(glm::vec3{2.0f, 0.0f, 0.0f});
    EXPECT_TRUE(body->is_active());

    world->update_fixed_step(c_fixed_step);

    EXPECT_GT(body->get_world_transform()[3][0], 0.0f);

    world->remove_rigid_body(body.get());
}
