#include "erhe_physics/joint_reach.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace {

using erhe::physics::Constraint_axis_limit;
using erhe::physics::Joint_reach;
using erhe::physics::Joint_reach_shape;
using erhe::physics::Joint_side;
using erhe::physics::Transform;

[[nodiscard]] auto fixed_axis(const float value = 0.0f) -> Constraint_axis_limit
{
    Constraint_axis_limit limit{};
    limit.limited = true;
    limit.min     = value;
    limit.max     = value;
    return limit;
}

[[nodiscard]] auto ranged_axis(const float min, const float max) -> Constraint_axis_limit
{
    Constraint_axis_limit limit{};
    limit.limited = true;
    limit.min     = min;
    limit.max     = max;
    return limit;
}

[[nodiscard]] auto free_axis() -> Constraint_axis_limit
{
    return Constraint_axis_limit{};
}

[[nodiscard]] auto all_fixed() -> std::array<Constraint_axis_limit, 6>
{
    std::array<Constraint_axis_limit, 6> limits{};
    for (Constraint_axis_limit& limit : limits) {
        limit = fixed_axis();
    }
    return limits;
}

[[nodiscard]] auto axis_vector(const int axis) -> glm::vec3
{
    glm::vec3 v{0.0f};
    v[axis] = 1.0f;
    return v;
}

[[nodiscard]] auto apply(const Transform& transform, const glm::vec3 point) -> glm::vec3
{
    return (transform.basis * point) + transform.origin;
}

[[nodiscard]] auto rotated_frame() -> Transform
{
    const glm::mat4 rotation = glm::rotate(glm::mat4{1.0f}, 0.7f, glm::normalize(glm::vec3{0.3f, 1.0f, -0.4f}));
    return Transform{glm::mat3{rotation}, glm::vec3{0.4f, 1.2f, -0.3f}};
}

// The point's world position at hinge angle theta about anchor axis k, from
// the joint convention directly: moving side a keeps F_a = F_b * R^T with the
// anchors' origins coincident (t = offset), side b keeps F_b = F_a * R.
[[nodiscard]] auto hinge_point(
    const Transform& fixed_anchor,
    const Joint_side side,
    const int        axis,
    const float      theta,
    const glm::vec3  offset,
    const glm::vec3  p
) -> glm::vec3
{
    const glm::mat3 rotation{glm::rotate(glm::mat4{1.0f}, theta, axis_vector(axis))};
    if (side == Joint_side::a) {
        // F_b.origin = F_a.origin + F_a.basis * t, F_a.basis = F_b.basis * R^T
        const Transform moving_anchor{fixed_anchor.basis * glm::transpose(rotation), glm::vec3{0.0f}};
        const glm::vec3 moving_origin = fixed_anchor.origin - (moving_anchor.basis * offset);
        return moving_origin + (moving_anchor.basis * p);
    }
    const Transform moving_anchor{fixed_anchor.basis * rotation, apply(fixed_anchor, offset)};
    return apply(moving_anchor, p);
}

// Projects target and checks against a dense sampling of the hinge angles.
void check_hinge_projection(
    Joint_reach&     reach,
    const Transform& fixed_anchor,
    const Joint_side side,
    const int        axis,
    const float      min,
    const float      max,
    const glm::vec3  offset,
    const glm::vec3  p,
    const glm::vec3  target
)
{
    const glm::vec3 projected = reach.project(target);
    float nearest_sample_distance = std::numeric_limits<float>::max();
    float projected_to_set        = std::numeric_limits<float>::max();
    constexpr int sample_count = 4000;
    for (int n = 0; n <= sample_count; ++n) {
        const float theta  = min + ((max - min) * static_cast<float>(n) / static_cast<float>(sample_count));
        const glm::vec3 q  = hinge_point(fixed_anchor, side, axis, theta, offset, p);
        nearest_sample_distance = std::min(nearest_sample_distance, glm::distance(q, target));
        projected_to_set        = std::min(projected_to_set,        glm::distance(q, projected));
    }
    EXPECT_LT(projected_to_set, 1.0e-3f) << "projected point is not reachable";
    EXPECT_LE(glm::distance(projected, target), nearest_sample_distance + 1.0e-3f) << "projected point is not the nearest";
}

} // anonymous namespace

TEST(joint_reach, hinge_circle_matches_joint_convention)
{
    const std::array<Transform, 2> frames{ Transform{glm::mat3{1.0f}, glm::vec3{0.0f, 1.0f, 0.0f}}, rotated_frame() };
    const std::array<glm::vec3, 5> targets{
        glm::vec3{ 1.0f,  1.0f,  0.0f},
        glm::vec3{ 0.3f,  0.2f,  0.4f},
        glm::vec3{-2.0f,  3.0f,  1.0f},
        glm::vec3{ 0.0f, -5.0f, -0.3f},
        glm::vec3{ 0.87f, 0.36f, -0.22f}
    };
    const glm::vec3 p{0.05f, -0.5f, 0.02f};
    for (const Transform& frame : frames) {
        for (const Joint_side side : { Joint_side::a, Joint_side::b }) {
            for (int axis = 0; axis < 3; ++axis) {
                std::array<Constraint_axis_limit, 6> limits = all_fixed();
                limits[3 + static_cast<std::size_t>(axis)] = ranged_axis(-1.4f, 0.6f);
                Joint_reach reach;
                reach.configure(frame, side, limits, p, hinge_point(frame, side, axis, 0.2f, glm::vec3{0.0f}, p), 0.0f);
                ASSERT_EQ(reach.get_shape(), Joint_reach_shape::circle);
                EXPECT_EQ(reach.get_axis(), axis);
                EXPECT_LT(glm::distance(reach.get_last_projected(), hinge_point(frame, side, axis, 0.2f, glm::vec3{0.0f}, p)), 1.0e-4f);
                for (const glm::vec3& target : targets) {
                    check_hinge_projection(reach, frame, side, axis, -1.4f, 0.6f, glm::vec3{0.0f}, p, target);
                }
            }
        }
    }
}

TEST(joint_reach, hinge_circle_with_fixed_nonzero_offset)
{
    const Transform frame = rotated_frame();
    const glm::vec3 offset{0.1f, -0.2f, 0.05f};
    const glm::vec3 p{0.3f, 0.1f, -0.2f};
    for (const Joint_side side : { Joint_side::a, Joint_side::b }) {
        std::array<Constraint_axis_limit, 6> limits = all_fixed();
        for (int i = 0; i < 3; ++i) {
            limits[static_cast<std::size_t>(i)] = fixed_axis(offset[i]);
        }
        limits[4] = free_axis();
        Joint_reach reach;
        reach.configure(frame, side, limits, p, hinge_point(frame, side, 1, 0.0f, offset, p), 0.0f);
        ASSERT_EQ(reach.get_shape(), Joint_reach_shape::circle);
        const float pi = glm::pi<float>();
        for (const glm::vec3& target : { glm::vec3{2.0f, 0.0f, 0.0f}, glm::vec3{-1.0f, 1.0f, 3.0f}, glm::vec3{0.0f, 0.0f, -2.0f} }) {
            check_hinge_projection(reach, frame, side, 1, -pi, pi, offset, p, target);
        }
    }
}

TEST(joint_reach, pivot_on_axis_is_a_point)
{
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[5] = free_axis();
    Joint_reach reach;
    const Transform frame{glm::mat3{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f}};
    reach.configure(frame, Joint_side::a, limits, glm::vec3{0.0f, 0.0f, 0.25f}, glm::vec3{1.0f, 2.0f, 3.25f}, 0.0f);
    EXPECT_EQ(reach.get_shape(), Joint_reach_shape::point);
    EXPECT_LT(glm::distance(reach.project(glm::vec3{9.0f, 9.0f, 9.0f}), glm::vec3{1.0f, 2.0f, 3.25f}), 1.0e-5f);
}

TEST(joint_reach, target_on_axis_keeps_last_projected)
{
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[5] = ranged_axis(-1.4f, 1.4f);
    const Transform frame{glm::mat3{1.0f}, glm::vec3{0.0f, 1.0f, 0.0f}};
    const glm::vec3 p{0.0f, -0.5f, 0.0f};
    Joint_reach reach;
    reach.configure(frame, Joint_side::a, limits, p, glm::vec3{0.0f, 0.5f, 0.0f}, 0.0f);
    const glm::vec3 first = reach.project(glm::vec3{0.3f, 0.6f, 0.0f});
    const glm::vec3 kept  = reach.project(glm::vec3{0.0f, 1.0f, 7.0f}); // on the hinge axis
    EXPECT_LT(glm::distance(first, kept), 1.0e-6f);
    EXPECT_NEAR(glm::distance(kept, glm::vec3{0.0f, 1.0f, 0.0f}), 0.5f, 1.0e-4f);
}

TEST(joint_reach, ball_sphere)
{
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[3] = free_axis();
    limits[4] = free_axis();
    limits[5] = ranged_axis(-0.5f, 0.5f);
    const Transform frame = rotated_frame();
    const glm::vec3 p{0.0f, -0.4f, 0.3f};
    Joint_reach reach;
    reach.configure(frame, Joint_side::b, limits, p, apply(frame, p), 0.0f);
    ASSERT_EQ(reach.get_shape(), Joint_reach_shape::sphere);
    EXPECT_NEAR(reach.get_radius(), 0.5f, 1.0e-5f);
    const glm::vec3 target{3.0f, -1.0f, 2.0f};
    const glm::vec3 projected = reach.project(target);
    EXPECT_NEAR(glm::distance(projected, frame.origin), 0.5f, 1.0e-4f);
    EXPECT_LT(glm::length(glm::cross(projected - frame.origin, target - frame.origin)), 1.0e-3f);
    // The center has no unique nearest point.
    EXPECT_LT(glm::distance(reach.project(frame.origin), projected), 1.0e-6f);
}

TEST(joint_reach, slider_box)
{
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[0] = ranged_axis(-0.2f, 0.5f);
    limits[2] = free_axis();
    const Transform frame{glm::mat3{1.0f}, glm::vec3{0.0f}};
    const glm::vec3 p{0.1f, 0.2f, 0.3f};
    {
        Joint_reach reach;
        reach.configure(frame, Joint_side::b, limits, p, p, 0.0f);
        ASSERT_EQ(reach.get_shape(), Joint_reach_shape::box);
        const glm::vec3 projected = reach.project(glm::vec3{5.0f, 5.0f, 5.0f});
        EXPECT_LT(glm::distance(projected, glm::vec3{0.6f, 0.2f, 5.0f}), 1.0e-5f);
    }
    {
        Joint_reach reach;
        reach.configure(frame, Joint_side::a, limits, p, p, 0.0f);
        const glm::vec3 projected = reach.project(glm::vec3{5.0f, 5.0f, -5.0f});
        EXPECT_LT(glm::distance(projected, glm::vec3{0.3f, 0.2f, -5.0f}), 1.0e-5f);
    }
}

TEST(joint_reach, all_fixed_is_a_point)
{
    const Transform frame = rotated_frame();
    const glm::vec3 p{0.1f, 0.2f, 0.3f};
    Joint_reach reach;
    reach.configure(frame, Joint_side::a, all_fixed(), p, apply(frame, p), 0.0f);
    EXPECT_EQ(reach.get_shape(), Joint_reach_shape::point);
    EXPECT_LT(glm::distance(reach.project(glm::vec3{4.0f}), apply(frame, p)), 1.0e-5f);
}

TEST(joint_reach, translation_and_rotation_is_unprojected)
{
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[1] = free_axis();
    limits[5] = free_axis();
    Joint_reach reach;
    reach.configure(Transform{}, Joint_side::a, limits, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, 0.0f);
    EXPECT_EQ(reach.get_shape(), Joint_reach_shape::unprojected);
    EXPECT_EQ(reach.project(glm::vec3{3.0f, 4.0f, 5.0f}), glm::vec3(3.0f, 4.0f, 5.0f));

    std::array<Constraint_axis_limit, 6> nonzero_rotation = all_fixed();
    nonzero_rotation[3] = fixed_axis(0.5f);
    nonzero_rotation[5] = free_axis();
    reach.configure(Transform{}, Joint_side::a, nonzero_rotation, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, 0.0f);
    EXPECT_EQ(reach.get_shape(), Joint_reach_shape::unprojected);
}

TEST(joint_reach, circle_margin_keeps_inside_the_range)
{
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[5] = ranged_axis(-1.4f, 1.4f);
    const Transform frame{glm::mat3{1.0f}, glm::vec3{0.0f, 1.0f, 0.0f}};
    const glm::vec3 p{0.0f, -0.5f, 0.0f};
    Joint_reach reach;
    reach.configure(frame, Joint_side::a, limits, p, glm::vec3{0.0f, 0.5f, 0.0f}, 0.1f);
    ASSERT_TRUE(reach.is_angle_limited());
    const float pi = glm::pi<float>();
    // Straight up is out of range: the reach ends 1.3 rad from straight down.
    const glm::vec3 projected = reach.project(glm::vec3{0.1f, 5.0f, 0.0f});
    const glm::vec3 d = projected - frame.origin;
    const float angle_from_down = std::acos(glm::dot(glm::normalize(d), glm::vec3{0.0f, -1.0f, 0.0f}));
    EXPECT_NEAR(angle_from_down, 1.3f, 1.0e-4f);
    EXPECT_LT(angle_from_down, pi);
}

TEST(joint_reach, circle_step_follows_the_arc)
{
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[5] = ranged_axis(-1.4f, 1.4f);
    const Transform frame{glm::mat3{1.0f}, glm::vec3{0.0f, 1.0f, 0.0f}};
    const glm::vec3 p{0.0f, -0.5f, 0.0f};
    Joint_reach reach;
    reach.configure(frame, Joint_side::a, limits, p, glm::vec3{0.0f, 0.5f, 0.0f}, 0.0f);
    const glm::vec3 from = reach.project(glm::vec3{-1.0f, 0.9f, 0.0f});
    const glm::vec3 to   = reach.project(glm::vec3{ 1.0f, 0.9f, 0.0f});
    glm::vec3 current = from;
    float     total   = 0.0f;
    int       steps   = 0;
    while ((glm::distance(current, to) > 1.0e-5f) && (steps < 10000)) {
        const glm::vec3 next = reach.step_toward(current, to, 0.01f);
        EXPECT_NEAR(glm::distance(next, frame.origin), 0.5f, 1.0e-4f);
        EXPECT_LE(next.y, 1.0f); // stays on the lower arc, inside the range
        total += glm::distance(next, current);
        current = next;
        ++steps;
    }
    EXPECT_LT(steps, 10000);
    EXPECT_NEAR(total, 0.5f * 2.8f, 0.02f); // the arc of the range, not the chord
}

TEST(joint_reach, sphere_step_follows_a_great_circle)
{
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[3] = free_axis();
    limits[4] = free_axis();
    limits[5] = free_axis();
    const Transform frame{glm::mat3{1.0f}, glm::vec3{0.0f}};
    Joint_reach reach;
    reach.configure(frame, Joint_side::b, limits, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, 0.0f);
    const glm::vec3 next = reach.step_toward(glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}, 0.1f);
    EXPECT_NEAR(glm::length(next), 1.0f, 1.0e-5f);
    EXPECT_NEAR(std::acos(next.x), 0.1f, 1.0e-4f);
}
