// The bone orientation math of doc/plans/rigging/skeleton_editing.md R16
// (rig/bone_roll.hpp): Recalculate Roll's angle, Align to Active's change and
// the frame change the verbs apply to a bone, its children and its rest.

#include "rig/bone_roll.hpp"

#include "erhe_scene/trs_transform.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <optional>

namespace {

using erhe::scene::Trs_transform;
using editor::Frame_change_head;
using editor::Roll_axis;

[[nodiscard]] auto close(const glm::vec3& a, const glm::vec3& b, const float tolerance = 1.0e-5f) -> bool
{
    return glm::length(a - b) < tolerance;
}

[[nodiscard]] auto mat_close(const glm::mat4& a, const glm::mat4& b, const float tolerance = 1.0e-5f) -> bool
{
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(a[column][row] - b[column][row]) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

// The angle in degrees between two directions.
[[nodiscard]] auto angle_deg(const glm::vec3& a, const glm::vec3& b) -> float
{
    const float cosine = glm::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f);
    return glm::degrees(std::acos(cosine));
}

[[nodiscard]] auto project(const glm::vec3& v, const glm::vec3& axis) -> glm::vec3
{
    return v - (glm::dot(v, axis) * axis);
}

const glm::quat c_world_rotation = glm::normalize(glm::angleAxis(1.1f, glm::normalize(glm::vec3{0.3f, 0.7f, -0.4f})));

// The chosen local axis ends up pointing, within the plane perpendicular to
// the bone axis, at the reference; the bone axis (and so the tail) does not
// move. For a +Y tail the X / Z axis is in that plane, so it points at the
// reference's projection exactly.
TEST(Bone_roll, roll_aims_the_chosen_axis_at_the_reference_and_keeps_the_bone_axis)
{
    const glm::vec3 tail{0.0f, 1.0f, 0.0f};
    const glm::vec3 references[] = {glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{-0.3f, 0.2f, 0.9f}};
    for (const Roll_axis axis : {Roll_axis::x, Roll_axis::z}) {
        for (const glm::vec3& reference : references) {
            const std::optional<float> angle = editor::compute_roll_angle(c_world_rotation, tail, editor::get_roll_axis_vector(axis), reference);
            ASSERT_TRUE(angle.has_value());
            const std::optional<glm::quat> change = editor::make_roll_change(tail, angle.value());
            ASSERT_TRUE(change.has_value());
            const glm::quat new_world    = c_world_rotation * change.value();
            const glm::vec3 bone_axis    = c_world_rotation * tail;
            const glm::vec3 aimed        = new_world * editor::get_roll_axis_vector(axis);
            EXPECT_LT(angle_deg(aimed, project(reference, glm::normalize(bone_axis))), 1.0e-4f) << "axis " << static_cast<int>(axis);
            EXPECT_TRUE(close(new_world * tail, bone_axis)) << "the bone axis and the tail stay";
        }
    }
}

// With a tail that is not perpendicular to the axis, the axis sweeps a cone;
// the roll turns its in-plane part onto the reference's in-plane part (the
// closest it gets) and a second roll is zero.
TEST(Bone_roll, roll_with_an_oblique_tail_aims_the_in_plane_part_and_is_idempotent)
{
    const glm::vec3 tail{0.3f, 1.0f, 0.2f};
    const glm::vec3 reference{0.1f, -0.4f, 1.0f};
    const glm::vec3 local_axis = editor::get_roll_axis_vector(Roll_axis::x);
    const std::optional<float> angle = editor::compute_roll_angle(c_world_rotation, tail, local_axis, reference);
    ASSERT_TRUE(angle.has_value());
    const glm::quat new_world = c_world_rotation * editor::make_roll_change(tail, angle.value()).value();
    const glm::vec3 bone_axis = glm::normalize(c_world_rotation * tail);
    EXPECT_LT(angle_deg(project(new_world * local_axis, bone_axis), project(reference, bone_axis)), 1.0e-4f);
    EXPECT_TRUE(close(new_world * tail, c_world_rotation * tail));
    const std::optional<float> again = editor::compute_roll_angle(new_world, tail, local_axis, reference);
    ASSERT_TRUE(again.has_value());
    EXPECT_LT(std::abs(glm::degrees(again.value())), 1.0e-3f);
}

TEST(Bone_roll, roll_is_undefined_for_a_reference_along_the_bone_axis_or_a_zero_tail)
{
    const glm::vec3 tail{0.0f, 1.0f, 0.0f};
    EXPECT_FALSE(editor::compute_roll_angle(c_world_rotation, tail, glm::vec3{1.0f, 0.0f, 0.0f}, c_world_rotation * tail).has_value());
    EXPECT_FALSE(editor::compute_roll_angle(c_world_rotation, glm::vec3{0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}).has_value());
    EXPECT_FALSE(editor::make_roll_change(glm::vec3{0.0f}, 1.0f).has_value());
}

// Align: equal local bone axes give the active bone's world rotation; unequal
// ones still give its world tail direction.
TEST(Bone_roll, align_gives_the_active_bones_direction_and_roll)
{
    const glm::quat active_rotation = glm::normalize(glm::angleAxis(-0.7f, glm::normalize(glm::vec3{0.9f, 0.1f, 0.4f})));
    const glm::vec3 y_tail{0.0f, 2.0f, 0.0f};
    const std::optional<glm::quat> change = editor::compute_align_change(c_world_rotation, y_tail, active_rotation, glm::vec3{0.0f, 0.5f, 0.0f});
    ASSERT_TRUE(change.has_value());
    const glm::quat new_world = c_world_rotation * change.value();
    EXPECT_GT(std::abs(glm::dot(new_world, active_rotation)), 1.0f - 1.0e-6f) << "same frame as the active bone";

    const glm::vec3 own_tail{0.2f, 1.0f, -0.1f};
    const glm::vec3 active_tail{0.0f, 0.3f, 0.6f};
    const std::optional<glm::quat> oblique = editor::compute_align_change(c_world_rotation, own_tail, active_rotation, active_tail);
    ASSERT_TRUE(oblique.has_value());
    EXPECT_LT(angle_deg((c_world_rotation * oblique.value()) * own_tail, active_rotation * active_tail), 1.0e-3f);
    EXPECT_FALSE(editor::compute_align_change(c_world_rotation, glm::vec3{0.0f}, active_rotation, active_tail).has_value());
}

// The frame change: the bone turns about its head; a child keeping its world
// transform, a connected child keeping its head on the (turned) tail and its
// world rotation; the pose relative to rest is unchanged.
TEST(Bone_roll, frame_change_keeps_children_and_the_pose_relative_to_rest)
{
    const glm::mat4 parent_world = glm::translate(glm::mat4{1.0f}, glm::vec3{0.5f, 1.0f, -0.3f}) * glm::mat4_cast(c_world_rotation);
    const Trs_transform bone_local{glm::vec3{0.1f, 0.8f, 0.0f}, glm::normalize(glm::angleAxis(0.4f, glm::vec3{0.0f, 0.0f, 1.0f})), glm::vec3{1.0f}};
    const Trs_transform bone_rest {glm::vec3{0.1f, 0.8f, 0.0f}, glm::normalize(glm::angleAxis(0.2f, glm::vec3{0.0f, 0.0f, 1.0f})), glm::vec3{1.0f}};
    const glm::quat     change = glm::normalize(glm::angleAxis(0.9f, glm::normalize(glm::vec3{0.2f, 1.0f, 0.1f})));
    const glm::quat     identity{1.0f, 0.0f, 0.0f, 0.0f};

    const Trs_transform new_bone_local = editor::apply_frame_change(bone_local, identity, change, Frame_change_head::keep_world);
    const Trs_transform new_bone_rest  = editor::apply_frame_change(bone_rest,  identity, change, Frame_change_head::keep_world);
    EXPECT_TRUE(close(new_bone_local.get_translation(), bone_local.get_translation())) << "the head stays";
    EXPECT_TRUE(mat_close(new_bone_local.get_matrix(), bone_local.get_matrix() * glm::mat4_cast(change)));
    EXPECT_TRUE(mat_close(new_bone_local.get_matrix() * glm::inverse(new_bone_rest.get_matrix()), bone_local.get_matrix() * glm::inverse(bone_rest.get_matrix())))
        << "the pose relative to rest is unchanged";

    const glm::mat4 bone_world     = parent_world * bone_local.get_matrix();
    const glm::mat4 new_bone_world = parent_world * new_bone_local.get_matrix();

    const Trs_transform child_local{glm::vec3{0.3f, 0.6f, -0.2f}, glm::normalize(glm::angleAxis(-0.3f, glm::vec3{1.0f, 0.0f, 0.0f})), glm::vec3{1.0f}};
    const Trs_transform new_child_local = editor::apply_frame_change(child_local, change, identity, Frame_change_head::keep_world);
    EXPECT_TRUE(mat_close(new_bone_world * new_child_local.get_matrix(), bone_world * child_local.get_matrix())) << "a child keeps its world transform";

    const glm::vec3 tail{0.0f, 0.6f, 0.0f};
    const Trs_transform connected_local{tail, glm::normalize(glm::angleAxis(0.5f, glm::vec3{0.0f, 1.0f, 0.0f})), glm::vec3{1.0f}};
    const Trs_transform new_connected_local = editor::apply_frame_change(connected_local, change, identity, Frame_change_head::on_parent_tail);
    const glm::mat4     new_connected_world = new_bone_world * new_connected_local.get_matrix();
    const glm::mat4     connected_world     = bone_world * connected_local.get_matrix();
    EXPECT_TRUE(close(glm::vec3{new_connected_world[3]}, glm::vec3{new_bone_world * glm::vec4{tail, 1.0f}})) << "the head stays on the tail";
    EXPECT_TRUE(mat_close(glm::mat4{glm::mat3{new_connected_world}}, glm::mat4{glm::mat3{connected_world}})) << "the world rotation is kept";
}

} // anonymous namespace
