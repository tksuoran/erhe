// The Symmetrize mirror math of doc/plans/rigging/skeleton_editing.md R13
// (rig/bone_mirror.hpp): the exact TRS mirror, the general parent-map form,
// and the Ik.* limit mapping under an X mirror.

#include "rig/bone_mirror.hpp"

#include "erhe_scene/trs_transform.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <cmath>

namespace {

using erhe::scene::Trs_transform;

[[nodiscard]] auto close(const glm::vec3& a, const glm::vec3& b, const float tolerance = 1.0e-5f) -> bool
{
    return glm::length(a - b) < tolerance;
}

[[nodiscard]] auto quat_close(const glm::quat& a, const glm::quat& b, const float tolerance = 1.0e-5f) -> bool
{
    return (1.0f - std::abs(glm::dot(glm::normalize(a), glm::normalize(b)))) < tolerance;
}

[[nodiscard]] auto mat_close(const glm::mat4& a, const glm::mat4& b, const float tolerance = 1.0e-4f) -> bool
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

[[nodiscard]] auto origin_of(const glm::mat4& m) -> glm::vec3
{
    return glm::vec3{m[3]};
}

const Trs_transform c_arm_local{
    glm::vec3{0.7f, 0.4f, -0.2f},
    glm::normalize(glm::angleAxis(0.6f, glm::normalize(glm::vec3{0.3f, -0.5f, 0.8f}))),
    glm::vec3{1.0f}
};

TEST(Bone_mirror, trs_mirror_is_s_times_transform_times_s_and_an_involution)
{
    const glm::mat4 s        = editor::get_mirror_x_matrix();
    const Trs_transform mirrored = editor::mirror_trs_x(c_arm_local);
    EXPECT_TRUE(mat_close(mirrored.get_matrix(), s * c_arm_local.get_matrix() * s));
    EXPECT_TRUE(close(mirrored.get_translation(), glm::vec3{-0.7f, 0.4f, -0.2f}));
    const Trs_transform back = editor::mirror_trs_x(mirrored);
    EXPECT_TRUE(close(back.get_translation(), c_arm_local.get_translation()));
    EXPECT_TRUE(quat_close(back.get_rotation(), c_arm_local.get_rotation()));
    EXPECT_TRUE(close(editor::mirror_vector_x(glm::vec3{0.1f, 1.0f, 0.2f}), glm::vec3{-0.1f, 1.0f, 0.2f}));
}

TEST(Bone_mirror, rotation_mirror_keeps_x_angles_and_negates_y_and_z_angles)
{
    const float angle = 0.4f;
    EXPECT_TRUE(quat_close(editor::mirror_rotation_x(glm::angleAxis(angle, glm::vec3{1.0f, 0.0f, 0.0f})), glm::angleAxis( angle, glm::vec3{1.0f, 0.0f, 0.0f})));
    EXPECT_TRUE(quat_close(editor::mirror_rotation_x(glm::angleAxis(angle, glm::vec3{0.0f, 1.0f, 0.0f})), glm::angleAxis(-angle, glm::vec3{0.0f, 1.0f, 0.0f})));
    EXPECT_TRUE(quat_close(editor::mirror_rotation_x(glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f})), glm::angleAxis(-angle, glm::vec3{0.0f, 0.0f, 1.0f})));
}

// The general form with the parent map S equals the exact TRS form.
TEST(Bone_mirror, general_form_with_the_mirror_as_parent_map_is_the_trs_mirror)
{
    const Trs_transform general = editor::mirror_local_transform(editor::get_mirror_x_matrix(), c_arm_local.get_matrix());
    const Trs_transform exact   = editor::mirror_trs_x(c_arm_local);
    EXPECT_TRUE(close(general.get_translation(), exact.get_translation()));
    EXPECT_TRUE(quat_close(general.get_rotation(), exact.get_rotation()));
    EXPECT_TRUE(close(general.get_scale(), exact.get_scale()));
}

// The general form under an arbitrary new parent: the mirror bone's world
// transform is S_w * W(bone) * S, S_w the reflection across the X = 0 plane
// of a rotated, translated skeleton frame; its head and tail are the mirror
// images of the source's across that plane.
TEST(Bone_mirror, general_form_mirrors_world_heads_and_tails_across_the_skeleton_frame_plane)
{
    const glm::mat4 frame = glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.5f, -1.0f}) * glm::mat4_cast(glm::angleAxis(0.8f, glm::normalize(glm::vec3{0.2f, 1.0f, 0.1f})));
    const glm::mat4 source_parent_world = frame * glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 1.2f, 0.1f}) * glm::mat4_cast(glm::angleAxis(0.3f, glm::vec3{1.0f, 0.0f, 0.0f}));
    // An unrelated new parent (e.g. a spine bone the mirror goes under).
    const glm::mat4 new_parent_world = frame * glm::translate(glm::mat4{1.0f}, glm::vec3{0.1f, 1.0f, 0.3f}) * glm::mat4_cast(glm::angleAxis(-0.5f, glm::normalize(glm::vec3{0.4f, 0.2f, 1.0f})));
    const glm::mat4 plane = editor::get_mirror_plane_matrix(frame);

    const glm::mat4     parent_map = glm::inverse(new_parent_world) * plane * source_parent_world;
    const Trs_transform new_local  = editor::mirror_local_transform(parent_map, c_arm_local.get_matrix());
    const glm::mat4     new_world  = new_parent_world * new_local.get_matrix();
    const glm::mat4     old_world  = source_parent_world * c_arm_local.get_matrix();
    EXPECT_TRUE(mat_close(new_world, plane * old_world * editor::get_mirror_x_matrix()));
    EXPECT_GT(glm::determinant(glm::mat3{new_world}), 0.0f) << "the mirror bone's frame is proper";

    const glm::vec3 tail{0.1f, 0.9f, -0.2f};
    const glm::vec3 old_head = origin_of(old_world);
    const glm::vec3 new_head = origin_of(new_world);
    const glm::vec3 old_tail = glm::vec3{old_world * glm::vec4{tail, 1.0f}};
    const glm::vec3 new_tail = glm::vec3{new_world * glm::vec4{editor::mirror_vector_x(tail), 1.0f}};
    EXPECT_TRUE(close(new_head, glm::vec3{plane * glm::vec4{old_head, 1.0f}}, 1.0e-5f));
    EXPECT_TRUE(close(new_tail, glm::vec3{plane * glm::vec4{old_tail, 1.0f}}, 1.0e-5f));

    // The plane is X = 0 of the frame: a head's frame-local x flips sign.
    const glm::vec3 old_in_frame = glm::vec3{glm::inverse(frame) * glm::vec4{old_head, 1.0f}};
    const glm::vec3 new_in_frame = glm::vec3{glm::inverse(frame) * glm::vec4{new_head, 1.0f}};
    EXPECT_TRUE(close(new_in_frame, glm::vec3{-old_in_frame.x, old_in_frame.y, old_in_frame.z}, 1.0e-5f));
}

// Limits: X range copied, Y and Z ranges negated and swapped.
TEST(Bone_mirror, ik_limits_keep_x_and_negate_and_swap_y_and_z)
{
    const editor::Ik_limit_range range = editor::mirror_ik_limits(glm::vec3{-0.1f, -0.2f, -0.3f}, glm::vec3{0.4f, 0.5f, 0.6f});
    EXPECT_TRUE(close(range.min, glm::vec3{-0.1f, -0.5f, -0.6f}));
    EXPECT_TRUE(close(range.max, glm::vec3{ 0.4f,  0.2f,  0.3f}));
    // An involution, and the coerced ranges map onto themselves.
    const editor::Ik_limit_range back = editor::mirror_ik_limits(range.min, range.max);
    EXPECT_TRUE(close(back.min, glm::vec3{-0.1f, -0.2f, -0.3f}));
    EXPECT_TRUE(close(back.max, glm::vec3{ 0.4f,  0.5f,  0.6f}));
    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_LE(range.min[axis], 0.0f);
        EXPECT_GE(range.max[axis], 0.0f);
    }
}

// Why the limit mapping holds: a limited rotation delta at the extreme of
// each range, mirrored (conjugated by S), is at the mirrored range's
// corresponding extreme - about X the same angle, about Y and Z the negated
// angle.
TEST(Bone_mirror, mirrored_limit_extremes_are_the_mirrored_deltas)
{
    const glm::vec3 limit_min{-0.1f, -0.2f, -0.3f};
    const glm::vec3 limit_max{ 0.4f,  0.5f,  0.6f};
    const editor::Ik_limit_range mirrored = editor::mirror_ik_limits(limit_min, limit_max);
    const glm::vec3 axes[3] = {glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 1.0f}};
    for (int axis = 0; axis < 3; ++axis) {
        const glm::quat at_max        = glm::angleAxis(limit_max[axis], axes[axis]);
        const glm::quat at_min        = glm::angleAxis(limit_min[axis], axes[axis]);
        const glm::quat mirrored_max  = editor::mirror_rotation_x(at_max);
        const glm::quat mirrored_min  = editor::mirror_rotation_x(at_min);
        if (axis == 0) {
            EXPECT_TRUE(quat_close(mirrored_max, glm::angleAxis(mirrored.max[axis], axes[axis])));
            EXPECT_TRUE(quat_close(mirrored_min, glm::angleAxis(mirrored.min[axis], axes[axis])));
        } else {
            EXPECT_TRUE(quat_close(mirrored_max, glm::angleAxis(mirrored.min[axis], axes[axis]))) << "axis " << axis;
            EXPECT_TRUE(quat_close(mirrored_min, glm::angleAxis(mirrored.max[axis], axes[axis]))) << "axis " << axis;
        }
    }
    EXPECT_FLOAT_EQ(editor::mirror_pole_angle(0.25f), -0.25f);
}

} // anonymous namespace
