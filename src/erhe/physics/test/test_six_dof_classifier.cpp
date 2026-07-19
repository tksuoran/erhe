#include "erhe_physics/box3d/box3d_six_dof_classifier.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/constants.hpp>

#include <array>
#include <cmath>

namespace {

using erhe::physics::Axis_state;
using erhe::physics::Constraint_axis_limit;
using erhe::physics::Six_dof_joint_kind;

// Helpers producing the three per-axis states.
[[nodiscard]] auto fixed_axis() -> Constraint_axis_limit
{
    Constraint_axis_limit limit{};
    limit.limited = true;
    limit.min     = 0.0f;
    limit.max     = 0.0f;
    return limit;
}

[[nodiscard]] auto limited_axis(const float min, const float max) -> Constraint_axis_limit
{
    Constraint_axis_limit limit{};
    limit.limited = true;
    limit.min     = min;
    limit.max     = max;
    return limit;
}

[[nodiscard]] auto free_axis() -> Constraint_axis_limit
{
    Constraint_axis_limit limit{};
    limit.limited = false;
    return limit;
}

[[nodiscard]] auto all_fixed() -> std::array<Constraint_axis_limit, 6>
{
    std::array<Constraint_axis_limit, 6> limits{};
    for (std::size_t i = 0; i < 6; ++i) {
        limits[i] = fixed_axis();
    }
    return limits;
}

[[nodiscard]] auto all_free() -> std::array<Constraint_axis_limit, 6>
{
    std::array<Constraint_axis_limit, 6> limits{};
    for (std::size_t i = 0; i < 6; ++i) {
        limits[i] = free_axis();
    }
    return limits;
}

// The vector the joint frame axis must be carried onto for each Box3D joint.
[[nodiscard]] auto rotate(const glm::quat& q, const glm::vec3& v) -> glm::vec3
{
    return q * v;
}

[[nodiscard]] auto axis_vector(const int axis) -> glm::vec3
{
    return glm::vec3{
        (axis == 0) ? 1.0f : 0.0f,
        (axis == 1) ? 1.0f : 0.0f,
        (axis == 2) ? 1.0f : 0.0f
    };
}

} // anonymous namespace

TEST(six_dof_classifier, axis_states)
{
    EXPECT_EQ(erhe::physics::classify_axis(fixed_axis()),          Axis_state::fixed);
    EXPECT_EQ(erhe::physics::classify_axis(free_axis()),           Axis_state::free);
    EXPECT_EQ(erhe::physics::classify_axis(limited_axis(-1, 1)),   Axis_state::limited);
    // An inverted range is sanitized to fixed, matching the Jolt backend.
    EXPECT_EQ(erhe::physics::classify_axis(limited_axis(1, -1)),   Axis_state::fixed);
}

TEST(six_dof_classifier, all_fixed_is_weld)
{
    const erhe::physics::Six_dof_classification classification = erhe::physics::classify_six_dof(all_fixed());
    EXPECT_EQ(classification.kind, Six_dof_joint_kind::weld);
    EXPECT_TRUE(classification.is_exact);
}

TEST(six_dof_classifier, all_free_is_filter)
{
    const erhe::physics::Six_dof_classification classification = erhe::physics::classify_six_dof(all_free());
    EXPECT_EQ(classification.kind, Six_dof_joint_kind::filter);
    EXPECT_TRUE(classification.is_exact);
}

TEST(six_dof_classifier, single_free_rotation_is_revolute)
{
    for (int axis = 0; axis < 3; ++axis) {
        std::array<Constraint_axis_limit, 6> limits = all_fixed();
        limits[static_cast<std::size_t>(3 + axis)] = free_axis();
        const erhe::physics::Six_dof_classification classification = erhe::physics::classify_six_dof(limits);
        EXPECT_EQ(classification.kind, Six_dof_joint_kind::revolute) << "axis " << axis;
        EXPECT_EQ(classification.axis, axis);
        EXPECT_TRUE(classification.is_exact);
    }
}

TEST(six_dof_classifier, single_limited_rotation_is_revolute)
{
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[4] = limited_axis(-0.5f, 0.5f);
    const erhe::physics::Six_dof_classification classification = erhe::physics::classify_six_dof(limits);
    EXPECT_EQ(classification.kind, Six_dof_joint_kind::revolute);
    EXPECT_EQ(classification.axis, 1);
    EXPECT_TRUE(classification.is_exact);
}

TEST(six_dof_classifier, single_free_translation_is_prismatic)
{
    for (int axis = 0; axis < 3; ++axis) {
        std::array<Constraint_axis_limit, 6> limits = all_fixed();
        limits[static_cast<std::size_t>(axis)] = free_axis();
        const erhe::physics::Six_dof_classification classification = erhe::physics::classify_six_dof(limits);
        EXPECT_EQ(classification.kind, Six_dof_joint_kind::prismatic) << "axis " << axis;
        EXPECT_EQ(classification.axis, axis);
        EXPECT_TRUE(classification.is_exact);
    }
}

TEST(six_dof_classifier, three_free_rotations_is_exact_spherical)
{
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[3] = free_axis();
    limits[4] = free_axis();
    limits[5] = free_axis();
    const erhe::physics::Six_dof_classification classification = erhe::physics::classify_six_dof(limits);
    EXPECT_EQ(classification.kind, Six_dof_joint_kind::spherical);
    EXPECT_TRUE(classification.is_exact);
}

TEST(six_dof_classifier, limited_spherical_is_inexact)
{
    // A cone limit couples two axes rather than limiting each independently.
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[3] = limited_axis(-0.4f, 0.4f);
    limits[4] = limited_axis(-0.4f, 0.4f);
    limits[5] = limited_axis(-0.4f, 0.4f);
    const erhe::physics::Six_dof_classification classification = erhe::physics::classify_six_dof(limits);
    EXPECT_EQ(classification.kind, Six_dof_joint_kind::spherical);
    EXPECT_FALSE(classification.is_exact);
}

TEST(six_dof_classifier, universal_joint_is_inexact_spherical)
{
    // Two rotational degrees of freedom: a spherical joint permits a third.
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[3] = free_axis();
    limits[4] = free_axis();
    const erhe::physics::Six_dof_classification classification = erhe::physics::classify_six_dof(limits);
    EXPECT_EQ(classification.kind, Six_dof_joint_kind::spherical);
    EXPECT_FALSE(classification.is_exact);
}

TEST(six_dof_classifier, mixed_translation_and_rotation_is_inexact)
{
    // A cylindrical joint (slide along and rotate about the same axis) has no
    // Box3D equivalent; the prismatic fallback keeps the translation.
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[0] = free_axis();
    limits[3] = free_axis();
    const erhe::physics::Six_dof_classification classification = erhe::physics::classify_six_dof(limits);
    EXPECT_FALSE(classification.is_exact);
    EXPECT_EQ(classification.kind, Six_dof_joint_kind::prismatic);
    EXPECT_EQ(classification.axis, 0);
}

TEST(six_dof_classifier, planar_joint_is_inexact)
{
    // Two free translation axes: not representable, and not a prismatic joint.
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[0] = free_axis();
    limits[2] = free_axis();
    const erhe::physics::Six_dof_classification classification = erhe::physics::classify_six_dof(limits);
    EXPECT_FALSE(classification.is_exact);
    EXPECT_EQ(classification.kind, Six_dof_joint_kind::weld);
}

TEST(six_dof_classifier, describe_axis_states)
{
    std::array<Constraint_axis_limit, 6> limits = all_fixed();
    limits[4] = free_axis();
    limits[5] = limited_axis(-1.0f, 1.0f);
    const erhe::physics::Six_dof_classification classification = erhe::physics::classify_six_dof(limits);
    EXPECT_EQ(erhe::physics::describe_axis_states(classification.axis_states), "FFF F-L");
}

TEST(six_dof_classifier, revolute_frame_rotation_carries_axis_onto_z)
{
    for (int axis = 0; axis < 3; ++axis) {
        const glm::vec3 rotated = rotate(erhe::physics::revolute_frame_rotation(axis), glm::vec3{0.0f, 0.0f, 1.0f});
        const glm::vec3 expected = axis_vector(axis);
        EXPECT_NEAR(rotated.x, expected.x, 1e-5f) << "axis " << axis;
        EXPECT_NEAR(rotated.y, expected.y, 1e-5f) << "axis " << axis;
        EXPECT_NEAR(rotated.z, expected.z, 1e-5f) << "axis " << axis;
    }
}

TEST(six_dof_classifier, prismatic_frame_rotation_carries_axis_onto_x)
{
    for (int axis = 0; axis < 3; ++axis) {
        const glm::vec3 rotated = rotate(erhe::physics::prismatic_frame_rotation(axis), glm::vec3{1.0f, 0.0f, 0.0f});
        const glm::vec3 expected = axis_vector(axis);
        EXPECT_NEAR(rotated.x, expected.x, 1e-5f) << "axis " << axis;
        EXPECT_NEAR(rotated.y, expected.y, 1e-5f) << "axis " << axis;
        EXPECT_NEAR(rotated.z, expected.z, 1e-5f) << "axis " << axis;
    }
}

TEST(six_dof_classifier, reduced_mass)
{
    EXPECT_NEAR(erhe::physics::reduced_mass(2.0f, 2.0f), 1.0f, 1e-6f);
    // A static body has infinite mass, so the reduced mass is the dynamic one.
    EXPECT_NEAR(erhe::physics::reduced_mass(0.0f, 5.0f), 5.0f, 1e-6f);
    EXPECT_NEAR(erhe::physics::reduced_mass(5.0f, 0.0f), 5.0f, 1e-6f);
    // Two static bodies: no meaningful mass.
    EXPECT_NEAR(erhe::physics::reduced_mass(0.0f, 0.0f), 0.0f, 1e-6f);
}

TEST(six_dof_classifier, stiffness_to_hertz)
{
    // omega = sqrt(k / m); hertz = omega / (2 pi)
    const float expected = std::sqrt(400.0f / 1.0f) / glm::two_pi<float>();
    EXPECT_NEAR(erhe::physics::stiffness_to_hertz(400.0f, 1.0f), expected, 1e-5f);
    // Zero means "rigid" to Box3D, which is the right reading of a missing
    // stiffness or an unusable mass.
    EXPECT_EQ(erhe::physics::stiffness_to_hertz(0.0f, 1.0f), 0.0f);
    EXPECT_EQ(erhe::physics::stiffness_to_hertz(400.0f, 0.0f), 0.0f);
}
