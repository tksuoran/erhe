#include "tools/joint_visualization.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using editor::Joint_line;
using editor::Joint_line_buffer;
using erhe::physics::Constraint_axis_limit;

constexpr float c_tolerance = 1.0e-4f;

[[nodiscard]] auto ranged_axis(const float min, const float max) -> Constraint_axis_limit
{
    return Constraint_axis_limit{.limited = true, .min = min, .max = max};
}

// A Jolt-convention hinge: twist about X limited, both swing axes and all
// translation fixed.
[[nodiscard]] auto make_hinge_input() -> editor::Physics_joint_line_input
{
    editor::Physics_joint_line_input input{};
    input.shape.translation = {ranged_axis(0.0f, 0.0f), ranged_axis(0.0f, 0.0f), ranged_axis(0.0f, 0.0f)};
    input.shape.twist_axis  = 0;
    input.shape.twist       = ranged_axis(-1.0f, 1.0f);
    input.shape.swing       = {ranged_axis(0.0f, 0.0f), ranged_axis(0.0f, 0.0f)};
    input.size              = 1.0f;
    input.style.arc_segments = 8;
    return input;
}

[[nodiscard]] auto count_color(const Joint_line_buffer& buffer, const glm::vec4& color) -> std::size_t
{
    std::size_t count = 0;
    for (const Joint_line& line : buffer.lines) {
        if (line.color == color) {
            ++count;
        }
    }
    return count;
}

TEST(Joint_visualization, hinge_draws_twist_arc_and_value_spoke)
{
    const editor::Physics_joint_line_input input = make_hinge_input();
    Joint_line_buffer buffer;
    editor::build_physics_joint_lines(input, buffer);
    // 8 arc segments + 2 range spokes in the limit color; no translation or
    // swing lines (everything else fixed).
    EXPECT_EQ(10u, count_color(buffer, input.style.limit_color));
    // Current twist spoke, value color, from frame B origin at angle 0 along +Y
    // at 0.6 * size.
    ASSERT_EQ(1u, count_color(buffer, input.style.value_color));
    for (const Joint_line& line : buffer.lines) {
        if (line.color == input.style.value_color) {
            EXPECT_NEAR(0.0f, line.p1.x, c_tolerance);
            EXPECT_NEAR(0.6f, line.p1.y, c_tolerance);
            EXPECT_NEAR(0.0f, line.p1.z, c_tolerance);
        }
    }
    // Every arc point lies at the arc radius from the frame origin, in the
    // plane perpendicular to the twist axis.
    for (const Joint_line& line : buffer.lines) {
        if (line.color == input.style.limit_color) {
            EXPECT_NEAR(0.0f, line.p1.x, c_tolerance);
            EXPECT_NEAR(0.6f, glm::length(line.p1), c_tolerance);
        }
    }
}

TEST(Joint_visualization, violated_twist_uses_the_violation_color)
{
    editor::Physics_joint_line_input input = make_hinge_input();
    input.coordinates.twist      = 1.5f;
    input.range_check.twist_ok   = false;
    Joint_line_buffer buffer;
    editor::build_physics_joint_lines(input, buffer);
    EXPECT_EQ(1u, count_color(buffer, input.style.violation_color));
    EXPECT_EQ(0u, count_color(buffer, input.style.value_color));
}

TEST(Joint_visualization, pending_joint_draws_only_in_the_pending_color)
{
    editor::Physics_joint_line_input input = make_hinge_input();
    input.live = false;
    Joint_line_buffer buffer;
    editor::build_physics_joint_lines(input, buffer);
    ASSERT_FALSE(buffer.lines.empty());
    EXPECT_EQ(buffer.lines.size(), count_color(buffer, input.style.pending_color));
}

TEST(Joint_visualization, approximated_limits_use_the_approximated_color)
{
    editor::Physics_joint_line_input input = make_hinge_input();
    input.shape.is_exact = false;
    Joint_line_buffer buffer;
    editor::build_physics_joint_lines(input, buffer);
    EXPECT_EQ(0u,  count_color(buffer, input.style.limit_color));
    EXPECT_EQ(10u, count_color(buffer, input.style.approximated_color));
}

TEST(Joint_visualization, limited_translation_is_a_segment_with_ticks)
{
    editor::Physics_joint_line_input input = make_hinge_input();
    input.shape.twist          = ranged_axis(0.0f, 0.0f);
    input.shape.translation[1] = ranged_axis(-0.2f, 0.5f);
    Joint_line_buffer buffer;
    editor::build_physics_joint_lines(input, buffer);
    // Segment + two end ticks.
    EXPECT_EQ(3u, count_color(buffer, input.style.limit_color));
    bool found_segment = false;
    for (const Joint_line& line : buffer.lines) {
        if ((line.color == input.style.limit_color) && (std::abs(line.p0.y + 0.2f) < c_tolerance) && (std::abs(line.p1.y - 0.5f) < c_tolerance)) {
            found_segment = true;
        }
    }
    EXPECT_TRUE(found_segment);
    // The current translation cross (three lines) in the value color.
    EXPECT_EQ(3u, count_color(buffer, input.style.value_color));
}

// The two backends state a hinge about Z differently - Jolt as a pyramid
// swing of the X twist axis about Z, Box3D as a twist about Z - but both
// constrain the same motion, so with the body's arm given both draw the same
// arc: centered on the arm, through the body, in the plane across Z.
[[nodiscard]] auto limit_points(const editor::Physics_joint_line_input& input) -> std::vector<glm::vec3>
{
    Joint_line_buffer buffer;
    editor::build_physics_joint_lines(input, buffer);
    std::vector<glm::vec3> points;
    for (const Joint_line& line : buffer.lines) {
        if ((line.color == input.style.limit_color) && (line.width == input.style.line_width)) {
            points.push_back(line.p0);
            points.push_back(line.p1);
        }
    }
    return points;
}

TEST(Joint_visualization, hinge_arc_follows_the_arm_on_both_conventions)
{
    const glm::vec3 arm{0.0f, -0.55f, 0.0f};

    editor::Physics_joint_line_input jolt = make_hinge_input();
    jolt.shape.twist    = ranged_axis(0.0f, 0.0f);
    jolt.shape.swing    = {ranged_axis(0.0f, 0.0f), ranged_axis(-0.785f, 0.785f)};
    jolt.arm_in_a       = arm;

    editor::Physics_joint_line_input box3d = make_hinge_input();
    box3d.shape.twist_axis = 2;
    box3d.shape.twist      = ranged_axis(-0.785f, 0.785f);
    box3d.shape.swing      = {ranged_axis(0.0f, 0.0f), ranged_axis(0.0f, 0.0f)};
    box3d.arm_in_a         = arm;

    for (const editor::Physics_joint_line_input* input : {&jolt, &box3d}) {
        const std::vector<glm::vec3> points = limit_points(*input);
        ASSERT_EQ(16u, points.size()); // 8 segments, both ends
        float min_x = 1.0f;
        float max_x = -1.0f;
        for (const glm::vec3& p : points) {
            EXPECT_NEAR(0.0f,  p.z,             c_tolerance);
            EXPECT_NEAR(0.55f, glm::length(p),  c_tolerance);
            EXPECT_LT(p.y, 0.0f); // below the pivot, around the hanging arm
            min_x = std::min(min_x, p.x);
            max_x = std::max(max_x, p.x);
        }
        // Symmetric about the arm: +-0.785 rad reaches +-0.55 sin(0.785).
        EXPECT_NEAR(-0.55f * std::sin(0.785f), min_x, 1.0e-3f);
        EXPECT_NEAR( 0.55f * std::sin(0.785f), max_x, 1.0e-3f);
    }
}

TEST(Joint_visualization, ik_limits_draw_boundary_current_and_twist)
{
    editor::Ik_joint_constraint constraint{};
    constraint.enabled    = true;
    constraint.twist_axis = 1;
    constraint.limit      = {true, true, true};
    constraint.limit_min  = glm::vec3{-0.5f, -0.4f, -0.3f};
    constraint.limit_max  = glm::vec3{ 0.5f,  0.4f,  0.6f};
    editor::Ik_swing_boundary boundary;
    editor::sample_ik_swing_boundary(constraint, constraint.rest_rotation, 4, boundary);

    editor::Ik_limit_line_input input{};
    input.constraint         = &constraint;
    input.boundary           = &boundary;
    input.current            = editor::decompose_ik_rotation(constraint, constraint.rest_rotation);
    input.length             = 2.0f;
    input.style.arc_segments = 6;
    Joint_line_buffer buffer;
    editor::build_ik_limit_lines(input, buffer);

    // 16 boundary segments + 4 spokes; 6 twist arc segments + 2 spokes; the
    // current direction and twist tick.
    EXPECT_EQ(20u, count_color(buffer, input.style.swing_color));
    EXPECT_EQ(8u,  count_color(buffer, input.style.twist_color));
    EXPECT_EQ(2u,  count_color(buffer, input.style.value_color));
    for (const Joint_line& line : buffer.lines) {
        if ((line.color == input.style.swing_color) && (line.width == input.style.line_width)) {
            EXPECT_NEAR(2.0f, glm::length(line.p1), c_tolerance);
        }
    }
}

} // anonymous namespace
