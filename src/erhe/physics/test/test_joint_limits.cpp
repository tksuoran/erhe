#include "erhe_physics/box3d_six_dof_classifier.hpp"
#include "erhe_physics/joint_limits.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>
#include <optional>
#include <string>

namespace {

using erhe::physics::Constraint_axis_limit;
using erhe::physics::Joint_coordinates;
using erhe::physics::Joint_limit_shape;
using erhe::physics::Joint_range_check;
using erhe::physics::Swing_limit_model;
using erhe::physics::Transform;

constexpr float c_tolerance = 1.0e-4f;

[[nodiscard]] auto ranged_axis(const float min, const float max) -> Constraint_axis_limit
{
    return Constraint_axis_limit{.limited = true, .min = min, .max = max};
}

[[nodiscard]] auto rotation_transform(const glm::quat& rotation, const glm::vec3 origin = glm::vec3{0.0f}) -> Transform
{
    return Transform{glm::mat3_cast(rotation), origin};
}

[[nodiscard]] auto pyramid_shape() -> Joint_limit_shape
{
    Joint_limit_shape shape{};
    shape.twist_axis  = 0;
    shape.swing_model = Swing_limit_model::pyramid;
    shape.twist       = ranged_axis(-0.5f, 0.5f);
    shape.swing       = {ranged_axis(-0.3f, 0.6f), ranged_axis(-0.2f, 0.4f)};
    return shape;
}

TEST(Joint_limits, swing_axes_are_the_other_two_in_order)
{
    EXPECT_EQ((std::array<int, 2>{1, 2}), erhe::physics::get_swing_axes(0));
    EXPECT_EQ((std::array<int, 2>{0, 2}), erhe::physics::get_swing_axes(1));
    EXPECT_EQ((std::array<int, 2>{0, 1}), erhe::physics::get_swing_axes(2));
}

TEST(Joint_limits, measures_translation_in_frame_a)
{
    const glm::quat rotation_a = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});
    const Transform frame_a    = rotation_transform(rotation_a, glm::vec3{1.0f, 2.0f, 3.0f});
    // One unit along frame A's X, which is world +Y.
    const Transform frame_b    = rotation_transform(rotation_a, glm::vec3{1.0f, 3.0f, 3.0f});
    const Joint_coordinates coordinates = erhe::physics::measure_joint_coordinates(frame_a, frame_b, pyramid_shape());
    EXPECT_NEAR(1.0f, coordinates.translation.x, c_tolerance);
    EXPECT_NEAR(0.0f, coordinates.translation.y, c_tolerance);
    EXPECT_NEAR(0.0f, coordinates.translation.z, c_tolerance);
}

TEST(Joint_limits, measures_twist_about_the_twist_axis)
{
    const glm::quat rotation_a = glm::angleAxis(0.7f, glm::normalize(glm::vec3{1.0f, 1.0f, 0.0f}));
    const glm::quat twist      = glm::angleAxis(0.4f, glm::vec3{1.0f, 0.0f, 0.0f});
    const Joint_coordinates coordinates = erhe::physics::measure_joint_coordinates(
        rotation_transform(rotation_a),
        rotation_transform(rotation_a * twist),
        pyramid_shape()
    );
    EXPECT_NEAR(0.4f, coordinates.twist,    c_tolerance);
    EXPECT_NEAR(0.0f, coordinates.swing[0], c_tolerance);
    EXPECT_NEAR(0.0f, coordinates.swing[1], c_tolerance);
    EXPECT_NEAR(0.0f, coordinates.cone,     c_tolerance);
}

TEST(Joint_limits, pyramid_direction_reproduces_the_measured_swing)
{
    // Jolt's pyramid swing: q = rot(Z, z) * rot(Y, y), whose x part the
    // decomposition moves into the twist.
    const glm::quat swing = glm::angleAxis(-0.3f, glm::vec3{0.0f, 0.0f, 1.0f}) * glm::angleAxis(0.5f, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat twist = glm::angleAxis(0.2f, glm::vec3{1.0f, 0.0f, 0.0f});
    const Joint_coordinates coordinates = erhe::physics::measure_joint_coordinates(
        Transform{},
        rotation_transform(swing * twist),
        pyramid_shape()
    );
    const glm::vec3 direction = erhe::physics::pyramid_swing_direction(0, coordinates.swing[0], coordinates.swing[1]);
    const glm::vec3 expected  = (swing * twist) * glm::vec3{1.0f, 0.0f, 0.0f};
    EXPECT_NEAR(expected.x, direction.x, c_tolerance);
    EXPECT_NEAR(expected.y, direction.y, c_tolerance);
    EXPECT_NEAR(expected.z, direction.z, c_tolerance);
}

TEST(Joint_limits, pure_swing_about_one_axis_measures_its_angle)
{
    const glm::quat swing = glm::angleAxis(0.45f, glm::vec3{0.0f, 1.0f, 0.0f});
    const Joint_coordinates coordinates = erhe::physics::measure_joint_coordinates(Transform{}, rotation_transform(swing), pyramid_shape());
    EXPECT_NEAR(0.45f, coordinates.swing[0], c_tolerance);
    EXPECT_NEAR(0.0f,  coordinates.swing[1], c_tolerance);
    EXPECT_NEAR(0.45f, coordinates.cone,     c_tolerance);
    EXPECT_NEAR(0.0f,  coordinates.twist,    c_tolerance);
}

TEST(Joint_limits, cone_angle_about_z_twist_axis)
{
    Joint_limit_shape shape{};
    shape.twist_axis  = 2;
    shape.swing_model = Swing_limit_model::cone;
    shape.cone        = ranged_axis(0.0f, 0.5f);
    const glm::quat swing = glm::angleAxis(0.6f, glm::normalize(glm::vec3{1.0f, 1.0f, 0.0f}));
    const Joint_coordinates coordinates = erhe::physics::measure_joint_coordinates(Transform{}, rotation_transform(swing), shape);
    EXPECT_NEAR(0.6f, coordinates.cone, c_tolerance);
    const Joint_range_check check = erhe::physics::check_joint_range(shape, coordinates, 1.0e-3f, 1.0e-3f);
    EXPECT_FALSE(check.swing_ok);
    EXPECT_TRUE (check.twist_ok);
}

TEST(Joint_limits, range_check_flags_only_the_violated_coordinate)
{
    Joint_limit_shape shape = pyramid_shape();
    shape.translation[1] = ranged_axis(-0.1f, 0.1f);
    Joint_coordinates coordinates{};
    coordinates.translation = glm::vec3{5.0f, 0.2f, 0.0f}; // X free, Y out of range
    coordinates.twist       = 0.4f;
    coordinates.swing       = {0.5f, 0.3f};
    const Joint_range_check check = erhe::physics::check_joint_range(shape, coordinates, 1.0e-3f, 1.0e-3f);
    EXPECT_TRUE (check.translation_ok[0]);
    EXPECT_FALSE(check.translation_ok[1]);
    EXPECT_TRUE (check.translation_ok[2]);
    EXPECT_TRUE (check.twist_ok);
    EXPECT_TRUE (check.swing_ok);
    EXPECT_FALSE(check.all_ok());
    coordinates.swing[1] = 0.45f;
    EXPECT_FALSE(erhe::physics::check_joint_range(shape, coordinates, 1.0e-3f, 1.0e-3f).swing_ok);
}

TEST(Joint_limits, pyramid_direction_of_zero_swing_is_the_twist_axis)
{
    for (int axis = 0; axis < 3; ++axis) {
        const glm::vec3 direction = erhe::physics::pyramid_swing_direction(axis, 0.0f, 0.0f);
        glm::vec3 expected{0.0f};
        expected[axis] = 1.0f;
        EXPECT_NEAR(expected.x, direction.x, c_tolerance);
        EXPECT_NEAR(expected.y, direction.y, c_tolerance);
        EXPECT_NEAR(expected.z, direction.z, c_tolerance);
    }
}

TEST(Joint_limits, contract_is_the_authored_limits_in_d6_form)
{
    std::array<Constraint_axis_limit, 6> limits{};
    limits[1] = ranged_axis(0.25f, 0.25f);
    limits[3] = ranged_axis(-0.4f, 0.3f);
    limits[4] = ranged_axis(-0.2f, 0.8f);
    const Joint_limit_shape shape = erhe::physics::get_contract_joint_limits(limits);
    EXPECT_EQ(0, shape.twist_axis);
    EXPECT_EQ(Swing_limit_model::pyramid, shape.swing_model);
    EXPECT_FLOAT_EQ(0.25f, shape.translation[1].min); // fixed at the authored value
    EXPECT_FLOAT_EQ(-0.4f, shape.twist.min);
    EXPECT_FLOAT_EQ( 0.8f, shape.swing[0].max);
    EXPECT_FALSE(shape.swing[1].limited);
    EXPECT_TRUE(shape.is_exact);
}

TEST(Joint_limits, box3d_compatibility)
{
    const Constraint_axis_limit fixed = ranged_axis(0.0f, 0.0f);
    // A hinge is a Box3D revolute joint.
    const std::array<Constraint_axis_limit, 6> hinge{fixed, fixed, fixed, fixed, fixed, ranged_axis(-0.7f, 0.7f)};
    EXPECT_FALSE(erhe::physics::describe_box3d_incompatibility(hinge).has_value());
    // A limited ball joint becomes one cone plus a twist.
    const std::array<Constraint_axis_limit, 6> ball{fixed, fixed, fixed, ranged_axis(-0.5f, 0.5f), ranged_axis(-0.5f, 0.5f), ranged_axis(-0.5f, 0.5f)};
    const std::optional<std::string> ball_note = erhe::physics::describe_box3d_incompatibility(ball);
    ASSERT_TRUE(ball_note.has_value());
    EXPECT_NE(std::string::npos, ball_note.value().find("cone"));
    // An axis fixed off zero folds into frame A.
    const std::array<Constraint_axis_limit, 6> off_zero{fixed, ranged_axis(0.1f, 0.1f), fixed, fixed, fixed, fixed};
    EXPECT_FALSE(erhe::physics::describe_box3d_incompatibility(off_zero).has_value());
    // Unless it is a rotation and a translation axis is not fixed.
    const std::array<Constraint_axis_limit, 6> off_zero_rotation{fixed, ranged_axis(-1.0f, 1.0f), fixed, ranged_axis(0.1f, 0.1f), fixed, fixed};
    const std::optional<std::string> rotation_note = erhe::physics::describe_box3d_incompatibility(off_zero_rotation);
    ASSERT_TRUE(rotation_note.has_value());
    EXPECT_NE(std::string::npos, rotation_note.value().find("fixed rotation"));
    // A radial translation range with free rotation is a distance joint.
    const Constraint_axis_limit rope = ranged_axis(0.0f, 1.0f);
    const std::array<Constraint_axis_limit, 6> tether{rope, rope, rope, Constraint_axis_limit{}, Constraint_axis_limit{}, Constraint_axis_limit{}};
    EXPECT_FALSE(erhe::physics::describe_box3d_incompatibility(tether).has_value());
}

TEST(Joint_limits, fixed_translation_folds_into_frame_a)
{
    // Frame A rotated 90 degrees about Z: its X axis is world +Y.
    const glm::quat rotation_a = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});
    Transform frame_a = rotation_transform(rotation_a, glm::vec3{1.0f, 2.0f, 3.0f});
    std::array<Constraint_axis_limit, 6> limits{};
    for (std::size_t axis = 0; axis < 6; ++axis) {
        limits[axis] = ranged_axis(0.0f, 0.0f);
    }
    limits[0] = ranged_axis(2.0f, 2.0f);
    limits[4] = Constraint_axis_limit{};
    const erhe::physics::Fixed_axis_fold fold = erhe::physics::fold_fixed_axis_values(frame_a, limits);
    EXPECT_TRUE(fold.exact);
    EXPECT_TRUE(fold.folded[0]);
    EXPECT_FALSE(fold.folded[1]);
    EXPECT_FLOAT_EQ(0.0f, limits[0].min);
    EXPECT_FLOAT_EQ(0.0f, limits[0].max);
    EXPECT_NEAR(1.0f, frame_a.origin.x, c_tolerance);
    EXPECT_NEAR(4.0f, frame_a.origin.y, c_tolerance);
    EXPECT_NEAR(3.0f, frame_a.origin.z, c_tolerance);
    // The basis is untouched by a translation fold.
    EXPECT_NEAR(0.0f, frame_a.basis[0].x, c_tolerance);
    EXPECT_NEAR(1.0f, frame_a.basis[0].y, c_tolerance);
}

TEST(Joint_limits, fixed_rotation_folds_into_frame_a_and_measures_zero)
{
    // A frame B twisted 0.3 about frame A's X and swung 0.2 about its Y reads
    // as those angles in the authored frame A and as zero in the folded one.
    Transform frame_a{};
    std::array<Constraint_axis_limit, 6> limits{};
    for (std::size_t axis = 0; axis < 6; ++axis) {
        limits[axis] = ranged_axis(0.0f, 0.0f);
    }
    limits[3] = ranged_axis(0.3f, 0.3f);
    limits[4] = ranged_axis(0.2f, 0.2f);
    const glm::quat q_b = erhe::physics::pyramid_swing_rotation(0, 0.2f, 0.0f) * glm::angleAxis(0.3f, glm::vec3{1.0f, 0.0f, 0.0f});
    const Transform frame_b = rotation_transform(q_b);

    const Joint_limit_shape contract = erhe::physics::get_contract_joint_limits(limits);
    const Joint_coordinates authored = erhe::physics::measure_joint_coordinates(frame_a, frame_b, contract);
    EXPECT_NEAR(0.3f, authored.twist,    c_tolerance);
    EXPECT_NEAR(0.2f, authored.swing[0], c_tolerance);

    const erhe::physics::Fixed_axis_fold fold = erhe::physics::fold_fixed_axis_values(frame_a, limits);
    EXPECT_TRUE(fold.exact);
    EXPECT_TRUE(fold.folded[3]);
    EXPECT_TRUE(fold.folded[4]);
    EXPECT_FALSE(fold.folded[5]);
    const Joint_coordinates folded = erhe::physics::measure_joint_coordinates(frame_a, frame_b, contract);
    EXPECT_NEAR(0.0f, folded.twist,    c_tolerance);
    EXPECT_NEAR(0.0f, folded.swing[0], c_tolerance);
    EXPECT_NEAR(0.0f, folded.swing[1], c_tolerance);

    // A non-fixed translation axis keeps the rotation from folding.
    Transform frame_a_2{};
    std::array<Constraint_axis_limit, 6> limits_2 = limits;
    limits_2[3] = ranged_axis(0.3f, 0.3f);
    limits_2[2] = ranged_axis(-1.0f, 1.0f);
    const erhe::physics::Fixed_axis_fold fold_2 = erhe::physics::fold_fixed_axis_values(frame_a_2, limits_2);
    EXPECT_FALSE(fold_2.exact);
    EXPECT_FALSE(fold_2.folded[3]);
    EXPECT_FLOAT_EQ(0.3f, limits_2[3].min);
}

} // anonymous namespace
