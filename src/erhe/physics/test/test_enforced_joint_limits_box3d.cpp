#include "erhe_physics/joint_limits.hpp"

#include <gtest/gtest.h>

#include <array>

namespace {

using erhe::physics::Constraint_axis_limit;
using erhe::physics::Joint_limit_shape;
using erhe::physics::Swing_limit_model;

[[nodiscard]] auto ranged_axis(const float min, const float max) -> Constraint_axis_limit
{
    return Constraint_axis_limit{.limited = true, .min = min, .max = max};
}

[[nodiscard]] auto fixed_axis() -> Constraint_axis_limit
{
    return ranged_axis(0.0f, 0.0f);
}

TEST(Enforced_joint_limits_box3d, revolute_twists_about_the_hinge_axis)
{
    const std::array<Constraint_axis_limit, 6> limits{
        fixed_axis(), fixed_axis(), fixed_axis(),
        fixed_axis(), ranged_axis(-1.0f, 1.2f), fixed_axis()
    };
    const Joint_limit_shape shape = erhe::physics::get_enforced_joint_limits(limits);
    EXPECT_EQ(1, shape.twist_axis);
    EXPECT_TRUE(shape.twist.limited);
    EXPECT_FLOAT_EQ(-1.0f, shape.twist.min);
    EXPECT_FLOAT_EQ( 1.2f, shape.twist.max);
    EXPECT_TRUE(shape.swing[0].limited);
    EXPECT_FLOAT_EQ(0.0f, shape.swing[0].max);
    EXPECT_TRUE(shape.is_exact);
}

TEST(Enforced_joint_limits_box3d, spherical_is_a_cone_of_the_widest_swing)
{
    const std::array<Constraint_axis_limit, 6> limits{
        fixed_axis(), fixed_axis(), fixed_axis(),
        ranged_axis(-0.2f, 0.4f), ranged_axis(-0.5f, 0.5f), ranged_axis(-0.3f, 0.3f)
    };
    const Joint_limit_shape shape = erhe::physics::get_enforced_joint_limits(limits);
    EXPECT_EQ(2, shape.twist_axis);
    EXPECT_EQ(Swing_limit_model::cone, shape.swing_model);
    EXPECT_TRUE(shape.cone.limited);
    EXPECT_FLOAT_EQ(0.5f, shape.cone.max);
    EXPECT_FLOAT_EQ(-0.3f, shape.twist.min);
    EXPECT_FALSE(shape.is_exact); // the asymmetric X range is not the cone
}

TEST(Enforced_joint_limits_box3d, limited_spherical_is_approximated)
{
    // Even a symmetric swing range is approximated: the cone couples the two
    // swing axes, which the six-DOF limits state independently
    // (classify_six_dof).
    const std::array<Constraint_axis_limit, 6> limits{
        fixed_axis(), fixed_axis(), fixed_axis(),
        ranged_axis(-0.5f, 0.5f), ranged_axis(-0.5f, 0.5f), Constraint_axis_limit{}
    };
    const Joint_limit_shape shape = erhe::physics::get_enforced_joint_limits(limits);
    EXPECT_EQ(Swing_limit_model::cone, shape.swing_model);
    EXPECT_FLOAT_EQ(0.5f, shape.cone.max);
    EXPECT_FALSE(shape.twist.limited);
    EXPECT_FALSE(shape.is_exact);
}

TEST(Enforced_joint_limits_box3d, free_ball_joint_is_exact)
{
    const std::array<Constraint_axis_limit, 6> limits{
        fixed_axis(), fixed_axis(), fixed_axis(),
        Constraint_axis_limit{}, Constraint_axis_limit{}, Constraint_axis_limit{}
    };
    const Joint_limit_shape shape = erhe::physics::get_enforced_joint_limits(limits);
    EXPECT_EQ(Swing_limit_model::cone, shape.swing_model);
    EXPECT_FALSE(shape.cone.limited);
    EXPECT_FALSE(shape.twist.limited);
    EXPECT_TRUE(shape.is_exact);
}

TEST(Enforced_joint_limits_box3d, universal_joint_locks_the_twist_about_the_fixed_axis)
{
    const std::array<Constraint_axis_limit, 6> limits{
        fixed_axis(), fixed_axis(), fixed_axis(),
        fixed_axis(), Constraint_axis_limit{}, Constraint_axis_limit{}
    };
    const Joint_limit_shape shape = erhe::physics::get_enforced_joint_limits(limits);
    EXPECT_EQ(0, shape.twist_axis);
    EXPECT_EQ(Swing_limit_model::cone, shape.swing_model);
    EXPECT_TRUE(shape.twist.limited);
    EXPECT_FLOAT_EQ(0.0f, shape.twist.min);
    EXPECT_FLOAT_EQ(0.0f, shape.twist.max);
    EXPECT_FALSE(shape.cone.limited);
    EXPECT_FALSE(shape.is_exact);
}

TEST(Enforced_joint_limits_box3d, fixed_translation_holds_its_authored_value)
{
    // Folded into frame A, so the hinge keeps the 1 m offset it was authored
    // with.
    const std::array<Constraint_axis_limit, 6> limits{
        ranged_axis(1.0f, 1.0f), fixed_axis(), fixed_axis(),
        Constraint_axis_limit{}, fixed_axis(), fixed_axis()
    };
    const Joint_limit_shape shape = erhe::physics::get_enforced_joint_limits(limits);
    EXPECT_EQ(0, shape.twist_axis);
    EXPECT_FALSE(shape.twist.limited);
    EXPECT_TRUE(shape.translation[0].limited);
    EXPECT_FLOAT_EQ(1.0f, shape.translation[0].min);
    EXPECT_FLOAT_EQ(1.0f, shape.translation[0].max);
    EXPECT_TRUE(shape.is_exact);
}

TEST(Enforced_joint_limits_box3d, radial_translation_range_is_a_sphere)
{
    const std::array<Constraint_axis_limit, 6> limits{
        ranged_axis(0.0f, 1.0f), ranged_axis(0.0f, 1.0f), ranged_axis(0.0f, 1.0f),
        Constraint_axis_limit{}, Constraint_axis_limit{}, Constraint_axis_limit{}
    };
    const Joint_limit_shape shape = erhe::physics::get_enforced_joint_limits(limits);
    EXPECT_EQ(erhe::physics::Translation_limit_model::sphere, shape.translation_model);
    EXPECT_TRUE(shape.distance.limited);
    EXPECT_FLOAT_EQ(0.0f, shape.distance.min);
    EXPECT_FLOAT_EQ(1.0f, shape.distance.max);
    EXPECT_FLOAT_EQ(-1.0f, shape.translation[2].min); // the bounding box
    EXPECT_FALSE(shape.twist.limited);
    EXPECT_FALSE(shape.cone.limited);
    EXPECT_TRUE(shape.is_exact);

    // Two meters straight along X lies outside the sphere on every axis entry.
    erhe::physics::Joint_coordinates coordinates{};
    coordinates.translation = glm::vec3{2.0f, 0.0f, 0.0f};
    coordinates.distance    = 2.0f;
    const erhe::physics::Joint_range_check check = erhe::physics::check_joint_range(shape, coordinates, 0.001f, 0.001f);
    EXPECT_FALSE(check.translation_ok[1]);
    EXPECT_FALSE(check.all_ok());
}

} // anonymous namespace
