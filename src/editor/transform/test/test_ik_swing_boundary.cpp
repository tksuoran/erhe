#include "transform/ik_solver.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace {

using editor::Ik_joint_constraint;
using editor::Ik_swing_boundary;

constexpr float c_tolerance = 1.0e-4f;

[[nodiscard]] auto same_rotation(const glm::quat& lhs, const glm::quat& rhs) -> bool
{
    // 1e-6 on the dot is about 0.16 degrees apart.
    return std::abs(glm::dot(lhs, rhs)) > (1.0f - 1.0e-6f);
}

// A constraint with a Y twist axis (the usual bone axis); swing axes X, Z.
[[nodiscard]] auto make_constraint() -> Ik_joint_constraint
{
    Ik_joint_constraint constraint{};
    constraint.enabled       = true;
    constraint.twist_axis    = 1;
    constraint.rest_rotation = glm::angleAxis(0.3f, glm::normalize(glm::vec3{1.0f, 0.0f, 1.0f}));
    return constraint;
}

// Scales the swing of a (rest-relative) swing rotation outward in the clamp
// space: the swing components grow by factor, w follows the unit norm.
[[nodiscard]] auto push_outward(const glm::quat& swing, const float factor) -> glm::quat
{
    const float x = swing.x * factor;
    const float z = swing.z * factor;
    const float w = std::sqrt(std::max(0.0f, 1.0f - (x * x) - (z * z)));
    return glm::quat{w, x, swing.y, z};
}

// Every sampled boundary rotation survives the clamp unchanged, and pushing
// it outward makes the clamp move it.
void expect_boundary_matches_clamp(const Ik_joint_constraint& constraint, const Ik_swing_boundary& boundary, const bool check_outward)
{
    const glm::quat start = constraint.rest_rotation; // inside every region
    ASSERT_FALSE(boundary.swings.empty());
    for (const glm::quat& swing : boundary.swings) {
        const glm::quat local   = constraint.rest_rotation * swing;
        const glm::quat clamped = editor::constrain_ik_local_rotation(constraint, start, local);
        EXPECT_TRUE(same_rotation(local, clamped));
        EXPECT_TRUE(editor::is_ik_rotation_within_limits(constraint, local, 1.0e-3f));
        if (check_outward && ((std::abs(swing.x) + std::abs(swing.z)) > 0.05f)) {
            const glm::quat outside         = constraint.rest_rotation * push_outward(swing, 1.05f);
            const glm::quat outside_clamped = editor::constrain_ik_local_rotation(constraint, start, outside);
            EXPECT_FALSE(same_rotation(outside, outside_clamped));
            EXPECT_FALSE(editor::is_ik_rotation_within_limits(constraint, outside, 1.0e-3f));
        }
    }
}

TEST(Ik_swing_boundary, both_swing_axes_limited_is_a_closed_quadrant_ellipse)
{
    Ik_joint_constraint constraint = make_constraint();
    constraint.limit     = {true, false, true};
    constraint.limit_min = glm::vec3{-0.4f, 0.0f, -0.9f};
    constraint.limit_max = glm::vec3{ 0.7f, 0.0f,  0.5f};
    Ik_swing_boundary boundary;
    editor::sample_ik_swing_boundary(constraint, constraint.rest_rotation, 8, boundary);
    ASSERT_EQ(1u, boundary.polyline_ends.size());
    EXPECT_EQ(33u, boundary.swings.size());
    EXPECT_TRUE(same_rotation(boundary.swings.front(), boundary.swings.back()));
    expect_boundary_matches_clamp(constraint, boundary, true);
}

TEST(Ik_swing_boundary, one_limited_swing_axis_is_two_bounds)
{
    Ik_joint_constraint constraint = make_constraint();
    constraint.limit     = {true, false, false};
    constraint.limit_min = glm::vec3{-0.4f, 0.0f, 0.0f};
    constraint.limit_max = glm::vec3{ 0.7f, 0.0f, 0.0f};
    Ik_swing_boundary boundary;
    editor::sample_ik_swing_boundary(constraint, constraint.rest_rotation, 8, boundary);
    ASSERT_EQ(2u, boundary.polyline_ends.size());
    expect_boundary_matches_clamp(constraint, boundary, false);
}

TEST(Ik_swing_boundary, locked_swing_axis_is_the_pinned_segment)
{
    Ik_joint_constraint constraint = make_constraint();
    constraint.lock      = {true, false, false};
    constraint.limit     = {false, false, true};
    constraint.limit_min = glm::vec3{0.0f, 0.0f, -0.6f};
    constraint.limit_max = glm::vec3{0.0f, 0.0f,  0.8f};
    // Pinned at a swing of 0.2 radians about X.
    const glm::quat pinned = constraint.rest_rotation * glm::angleAxis(0.2f, glm::vec3{1.0f, 0.0f, 0.0f});
    Ik_swing_boundary boundary;
    editor::sample_ik_swing_boundary(constraint, pinned, 8, boundary);
    ASSERT_EQ(1u, boundary.polyline_ends.size());
    for (const glm::quat& swing : boundary.swings) {
        EXPECT_NEAR(std::sin(0.1f), swing.x, c_tolerance);
        const glm::quat local   = constraint.rest_rotation * swing;
        const glm::quat clamped = editor::constrain_ik_local_rotation(constraint, pinned, local);
        EXPECT_TRUE(same_rotation(local, clamped));
    }
    EXPECT_NEAR(std::sin(-0.3f), boundary.swings.front().z, c_tolerance);
    EXPECT_NEAR(std::sin( 0.4f), boundary.swings.back ().z, c_tolerance);
}

TEST(Ik_swing_boundary, both_swing_axes_locked_draws_nothing)
{
    Ik_joint_constraint constraint = make_constraint();
    constraint.lock = {true, false, true};
    Ik_swing_boundary boundary;
    editor::sample_ik_swing_boundary(constraint, constraint.rest_rotation, 8, boundary);
    EXPECT_TRUE(boundary.swings.empty());
    EXPECT_TRUE(boundary.polyline_ends.empty());
}

TEST(Ik_swing_boundary, twist_angle_is_measured_about_the_twist_axis)
{
    Ik_joint_constraint constraint = make_constraint();
    constraint.limit     = {false, true, false};
    constraint.limit_min = glm::vec3{0.0f, -0.5f, 0.0f};
    constraint.limit_max = glm::vec3{0.0f,  0.5f, 0.0f};
    const glm::quat inside  = constraint.rest_rotation * glm::angleAxis( 0.4f, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat outside = constraint.rest_rotation * glm::angleAxis(-0.6f, glm::vec3{0.0f, 1.0f, 0.0f});
    EXPECT_NEAR( 0.4f, editor::decompose_ik_rotation(constraint, inside ).twist_angle, c_tolerance);
    EXPECT_NEAR(-0.6f, editor::decompose_ik_rotation(constraint, outside).twist_angle, c_tolerance);
    EXPECT_TRUE (editor::is_ik_rotation_within_limits(constraint, inside,  1.0e-4f));
    EXPECT_FALSE(editor::is_ik_rotation_within_limits(constraint, outside, 1.0e-4f));
}

} // anonymous namespace
