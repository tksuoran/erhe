#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_physics/jolt/jolt_rigid_body.hpp"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/MotionProperties.h>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <memory>

// Which scales the Jolt backend represents exactly (ICollision_shape::is_valid_scale()).

using erhe::physics::Axis;
using erhe::physics::ICollision_shape;

TEST(Shape_scale_jolt, a_box_and_a_hull_take_any_scale)
{
    const std::shared_ptr<ICollision_shape> box = ICollision_shape::create_box_shape_shared(glm::vec3{0.5f});
    EXPECT_TRUE(box->is_valid_scale(glm::vec3{3.0f, 1.0f, 0.5f}));
    const float points[] = {
        0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f
    };
    const std::shared_ptr<ICollision_shape> hull = ICollision_shape::create_convex_hull_shape_shared(points, 4, 3 * sizeof(float));
    ASSERT_TRUE(hull);
    EXPECT_TRUE(hull->is_valid_scale(glm::vec3{3.0f, 1.0f, 0.5f}));
}

TEST(Shape_scale_jolt, a_sphere_and_a_capsule_take_only_a_uniform_scale)
{
    const std::shared_ptr<ICollision_shape> sphere = ICollision_shape::create_sphere_shape_shared(0.5f);
    EXPECT_TRUE (sphere->is_valid_scale(glm::vec3{2.0f}));
    EXPECT_FALSE(sphere->is_valid_scale(glm::vec3{3.0f, 1.0f, 1.0f}));
    const glm::vec3 valid = sphere->make_scale_valid(glm::vec3{3.0f, 1.0f, 1.0f});
    EXPECT_TRUE(sphere->is_valid_scale(valid));

    const std::shared_ptr<ICollision_shape> capsule = ICollision_shape::create_capsule_shape_shared(Axis::Y, 0.5f, 1.0f);
    EXPECT_FALSE(capsule->is_valid_scale(glm::vec3{1.0f, 2.0f, 1.0f}));
}

TEST(Shape_scale_jolt, no_shape_takes_a_zero_component)
{
    const std::shared_ptr<ICollision_shape> box = ICollision_shape::create_box_shape_shared(glm::vec3{0.5f});
    EXPECT_FALSE(box->is_valid_scale(glm::vec3{1.0f, 0.0f, 1.0f}));
}

TEST(Shape_scale_jolt, a_static_body_given_a_scaled_shape_simulates_the_scaled_mass_once_dynamic)
{
    using erhe::physics::IRigid_body;
    using erhe::physics::IRigid_body_create_info;
    using erhe::physics::IWorld;
    using erhe::physics::Motion_mode;
    std::unique_ptr<IWorld> world = IWorld::create_unique();
    const std::shared_ptr<ICollision_shape> unit_box = ICollision_shape::create_box_shape_shared(glm::vec3{0.5f});
    IRigid_body_create_info create_info;
    create_info.collision_shape = unit_box;
    create_info.motion_mode     = Motion_mode::e_static;
    create_info.debug_label     = "wall";
    const std::shared_ptr<IRigid_body> body = world->create_rigid_body_shared(create_info);
    world->add_rigid_body(body.get());

    body->set_collision_shape(ICollision_shape::create_uniform_scaling_shape_shared(unit_box, 2.0f));
    body->set_motion_mode(Motion_mode::e_dynamic);

    // A 2 m cube of density 1: mass 8, inertia 8 * (4 + 4) / 12 about each axis.
    const JPH::MotionProperties* const motion = static_cast<erhe::physics::Jolt_rigid_body*>(body.get())->get_jolt_body()->GetMotionProperties();
    ASSERT_NE(motion, nullptr);
    EXPECT_NEAR(body->get_mass(), 8.0f, 1.0e-3f);
    EXPECT_NEAR(motion->GetInverseMass(), 1.0f / 8.0f, 1.0e-5f);
    EXPECT_NEAR(motion->GetInverseInertiaDiagonal().GetX(), 12.0f / 64.0f, 1.0e-4f);
    world->remove_rigid_body(body.get());
}
