#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/iworld.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <memory>

// A body created static and switched to dynamic collides like one created dynamic.

using erhe::physics::ICollision_shape;
using erhe::physics::IRigid_body;
using erhe::physics::IRigid_body_create_info;
using erhe::physics::IWorld;
using erhe::physics::Motion_mode;

TEST(Motion_mode_change, a_static_body_made_dynamic_lands_on_a_static_body)
{
    std::unique_ptr<IWorld> world = IWorld::create_unique();
    world->set_gravity(glm::vec3{0.0f, -9.81f, 0.0f});

    IRigid_body_create_info ground_info;
    ground_info.collision_shape = ICollision_shape::create_box_shape_shared(glm::vec3{5.0f, 0.5f, 5.0f});
    ground_info.motion_mode     = Motion_mode::e_static;
    ground_info.position        = glm::vec3{0.0f, -0.5f, 0.0f};
    ground_info.debug_label     = "ground";
    const std::shared_ptr<IRigid_body> ground = world->create_rigid_body_shared(ground_info);
    world->add_rigid_body(ground.get());

    IRigid_body_create_info block_info;
    block_info.collision_shape = ICollision_shape::create_box_shape_shared(glm::vec3{0.25f});
    block_info.motion_mode     = Motion_mode::e_static;
    block_info.position        = glm::vec3{0.0f, 1.0f, 0.0f};
    block_info.debug_label     = "block";
    const std::shared_ptr<IRigid_body> block = world->create_rigid_body_shared(block_info);
    world->add_rigid_body(block.get());
    block->set_motion_mode(Motion_mode::e_dynamic);

    for (int step = 0; step < 120; ++step) {
        world->update_fixed_step(1.0 / 60.0);
    }
    const float y = glm::vec3{block->get_world_transform()[3]}.y;
    EXPECT_NEAR(y, 0.25f, 0.05f);

    world->remove_rigid_body(block.get());
    world->remove_rigid_body(ground.get());
}
