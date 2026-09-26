#include "erhe_physics/joint_limits.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/constants.hpp>

#include <array>

namespace {

using erhe::physics::Constraint_axis_limit;
using erhe::physics::Joint_limit_shape;
using erhe::physics::Swing_limit_model;

[[nodiscard]] auto ranged_axis(const float min, const float max) -> Constraint_axis_limit
{
    return Constraint_axis_limit{.limited = true, .min = min, .max = max};
}

TEST(Enforced_joint_limits_jolt, hinge_about_y_is_pyramid_twist_x)
{
    const std::array<Constraint_axis_limit, 6> limits{
        ranged_axis(0.0f, 0.0f), ranged_axis(0.0f, 0.0f), ranged_axis(0.0f, 0.0f),
        ranged_axis(0.0f, 0.0f), ranged_axis(-1.0f, 1.2f), ranged_axis(0.0f, 0.0f)
    };
    const Joint_limit_shape shape = erhe::physics::get_enforced_joint_limits(limits);
    EXPECT_EQ(0, shape.twist_axis);
    EXPECT_EQ(Swing_limit_model::pyramid, shape.swing_model);
    EXPECT_TRUE(shape.swing[0].limited);
    EXPECT_FLOAT_EQ(-1.0f, shape.swing[0].min);
    EXPECT_FLOAT_EQ( 1.2f, shape.swing[0].max);
    EXPECT_TRUE(shape.is_exact);
}

TEST(Enforced_joint_limits_jolt, fixed_translation_is_fixed_at_zero)
{
    std::array<Constraint_axis_limit, 6> limits{};
    limits[1] = ranged_axis(0.25f, 0.25f);
    const Joint_limit_shape shape = erhe::physics::get_enforced_joint_limits(limits);
    EXPECT_TRUE(shape.translation[1].limited);
    EXPECT_FLOAT_EQ(0.0f, shape.translation[1].min);
    EXPECT_FLOAT_EQ(0.0f, shape.translation[1].max);
    EXPECT_FALSE(shape.is_exact);
}

TEST(Enforced_joint_limits_jolt, rotation_clamped_locked_and_freed)
{
    std::array<Constraint_axis_limit, 6> limits{};
    limits[3] = ranged_axis(-4.0f, 0.5f);     // clamped to -pi
    limits[4] = ranged_axis(-0.001f, 0.001f); // inside +-0.5 degrees: locked at zero
    limits[5] = ranged_axis(-3.14f, 3.14f);   // wider than +-179.5 degrees: free
    const Joint_limit_shape shape = erhe::physics::get_enforced_joint_limits(limits);
    EXPECT_FLOAT_EQ(-glm::pi<float>(), shape.twist.min);
    EXPECT_FLOAT_EQ(0.0f, shape.swing[0].min);
    EXPECT_FLOAT_EQ(0.0f, shape.swing[0].max);
    EXPECT_FALSE(shape.swing[1].limited);
    EXPECT_FALSE(shape.is_exact);
}

} // anonymous namespace
