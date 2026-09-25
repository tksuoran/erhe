// The posing verbs of doc/plans/rigging/skeleton_editing.md slice B: the
// Clear channel rule (R14), the Paste Pose Flipped mirror math (R15) and the
// plans the commands apply, on bones whose rest transforms are authored as
// Rig.rest_* local values (no skin is involved, so the defaults are identity).

#include "rig/bone_pose.hpp"
#include "scene/rig_properties.hpp"

#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using Node = erhe::scene::Node;
using erhe::scene::Trs_transform;
using editor::Bone_pose;
using editor::Bone_pose_plan;
using editor::Paste_pose_mode;
using editor::Pose_channels;
using editor::Rig;

const glm::mat4 c_mirror_x = glm::scale(glm::mat4{1.0f}, glm::vec3{-1.0f, 1.0f, 1.0f});

[[nodiscard]] auto close(const glm::vec3& a, const glm::vec3& b, const float tolerance = 1.0e-4f) -> bool
{
    return glm::length(a - b) < tolerance;
}

[[nodiscard]] auto quat_close(const glm::quat& a, const glm::quat& b, const float tolerance = 1.0e-4f) -> bool
{
    return (1.0f - std::abs(glm::dot(glm::normalize(a), glm::normalize(b)))) < tolerance;
}

[[nodiscard]] auto mat_close(const glm::mat4& a, const glm::mat4& b, const float tolerance = 1.0e-4f) -> bool
{
    for (int column = 0; column < 4; ++column) {
        if (glm::length(a[column] - b[column]) > tolerance) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto trs(const glm::vec3& translation, const float angle, const glm::vec3& axis) -> glm::mat4
{
    return glm::translate(glm::mat4{1.0f}, translation) * glm::rotate(glm::mat4{1.0f}, angle, glm::normalize(axis));
}

[[nodiscard]] auto make_bone(const std::string& name, const std::shared_ptr<Node>& parent) -> std::shared_ptr<Node>
{
    std::shared_ptr<Node> node = std::make_shared<erhe::scene::Xform>(name);
    node->enable_flag_bits(erhe::Item_flags::bone);
    if (parent) {
        node->set_parent(parent);
    }
    return node;
}

void set_rest(Node& bone, const Trs_transform& rest)
{
    bone.set_value(Rig::rest_translation_property(), rest.get_translation());
    bone.set_value(Rig::rest_rotation_property(),    rest.get_rotation());
    bone.set_value(Rig::rest_scale_property(),       rest.get_scale());
}

// R14: the chosen channels come from the rest, the others stay.
TEST(Bone_pose, clear_channels_replaces_only_the_chosen_channels)
{
    const Trs_transform current{glm::vec3{1.0f, 2.0f, 3.0f}, glm::angleAxis(0.7f, glm::vec3{0.0f, 0.0f, 1.0f}), glm::vec3{2.0f}};
    const Trs_transform rest   {glm::vec3{0.0f, 1.0f, 0.0f}, glm::angleAxis(0.1f, glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{1.0f}};

    const Trs_transform rotation_only = editor::clear_pose_channels(current, rest, Pose_channels{.rotation = true}, 0);
    EXPECT_TRUE(close     (rotation_only.get_translation(), current.get_translation()));
    EXPECT_TRUE(quat_close(rotation_only.get_rotation(),    rest.get_rotation()));
    EXPECT_TRUE(close     (rotation_only.get_scale(),       current.get_scale()));

    const Trs_transform all = editor::clear_pose_channels(current, rest, Pose_channels{.location = true, .rotation = true, .scale = true}, 0);
    EXPECT_TRUE(mat_close(all.get_matrix(), rest.get_matrix()));
}

// R14: a locked axis keeps its current value (the Transform tool's rule).
TEST(Bone_pose, clear_channels_keeps_locked_axes)
{
    const Trs_transform current{glm::vec3{1.0f, 2.0f, 3.0f}, glm::angleAxis(0.5f, glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{2.0f}};
    const Trs_transform rest   {glm::vec3{0.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{1.0f}};
    const uint64_t locks = erhe::Item_flags::lock_translation_y | erhe::Item_flags::lock_rotation_x | erhe::Item_flags::lock_scale_z;

    const Trs_transform cleared = editor::clear_pose_channels(current, rest, Pose_channels{.location = true, .rotation = true, .scale = true}, locks);
    EXPECT_TRUE(close(cleared.get_translation(), glm::vec3{0.0f, 2.0f, 0.0f})) << "y is locked";
    EXPECT_NEAR(glm::eulerAngles(cleared.get_rotation()).x, 0.5f, 1.0e-4f) << "the rotation about X is locked";
    EXPECT_TRUE(close(cleared.get_scale(), glm::vec3{1.0f, 1.0f, 2.0f})) << "z scale is locked";
}

// R15: the pose equal to the source's rest pastes as the target's rest.
TEST(Bone_pose, mirror_of_the_rest_pose_is_the_target_rest)
{
    const glm::mat4 source_rest   = trs(glm::vec3{0.1f, 0.2f, 0.0f}, 0.4f, glm::vec3{1.0f, 2.0f, 3.0f});
    const glm::mat4 target_rest   = trs(glm::vec3{-0.1f, 0.2f, 0.0f}, -0.9f, glm::vec3{3.0f, -1.0f, 0.5f});
    const glm::mat4 parent_source = trs(glm::vec3{0.0f, 1.0f, 0.0f}, 0.3f, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::mat4 parent_target = trs(glm::vec3{0.0f, 1.0f, 0.0f}, -0.3f, glm::vec3{0.0f, 1.0f, 0.0f});
    EXPECT_TRUE(mat_close(editor::mirror_pose_transform(source_rest, source_rest, parent_source, target_rest, parent_target), target_rest));
}

// R15: with rest frames that are mirror images (B(target) = S B(source) S at
// every level), the result is S P S: translation x negated, quaternion
// (w, x, y, z) -> (w, x, -y, -z).
TEST(Bone_pose, mirror_with_mirror_image_rests_negates_x_and_the_y_z_quaternion)
{
    const glm::mat4 source_parent = trs(glm::vec3{0.2f, 1.0f, 0.1f}, 0.6f, glm::vec3{1.0f, 1.0f, 0.0f});
    const glm::mat4 target_parent = c_mirror_x * source_parent * c_mirror_x;
    const glm::mat4 source_rest   = trs(glm::vec3{0.0f, 0.3f, 0.0f}, 0.25f, glm::vec3{0.0f, 0.0f, 1.0f});
    const glm::mat4 target_rest   = c_mirror_x * source_rest * c_mirror_x;

    const glm::quat pose_rotation = glm::normalize(glm::quat{0.9f, 0.2f, -0.3f, 0.25f});
    const glm::vec3 pose_translation{0.05f, 0.3f, -0.02f};
    const glm::mat4 source_pose = glm::translate(glm::mat4{1.0f}, pose_translation) * glm::mat4_cast(pose_rotation);

    const Trs_transform mirrored{editor::mirror_pose_transform(source_pose, source_rest, source_parent, target_rest, target_parent)};
    EXPECT_TRUE(close(mirrored.get_translation(), glm::vec3{-pose_translation.x, pose_translation.y, pose_translation.z}));
    EXPECT_TRUE(quat_close(mirrored.get_rotation(), glm::quat{pose_rotation.w, pose_rotation.x, -pose_rotation.y, -pose_rotation.z}));
}

// R15: mirroring the result back gives the source pose.
TEST(Bone_pose, mirror_is_an_involution)
{
    const glm::mat4 source_rest   = trs(glm::vec3{0.1f, 0.2f, 0.0f}, 0.4f, glm::vec3{1.0f, 2.0f, 3.0f});
    const glm::mat4 target_rest   = trs(glm::vec3{-0.1f, 0.2f, 0.0f}, -0.9f, glm::vec3{3.0f, -1.0f, 0.5f});
    const glm::mat4 parent_source = trs(glm::vec3{0.0f, 1.0f, 0.0f}, 0.3f, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::mat4 parent_target = trs(glm::vec3{0.0f, 1.0f, 0.2f}, -0.5f, glm::vec3{1.0f, 1.0f, 0.0f});
    const glm::mat4 source_pose   = trs(glm::vec3{0.12f, 0.18f, 0.03f}, 1.1f, glm::vec3{0.3f, -1.0f, 0.7f});

    const glm::mat4 there = editor::mirror_pose_transform(source_pose, source_rest, parent_source, target_rest, parent_target);
    const glm::mat4 back  = editor::mirror_pose_transform(there, target_rest, parent_target, source_rest, parent_source);
    EXPECT_TRUE(mat_close(back, source_pose));
}

// R15: two-bone arms on a shared parent whose rest frames are mirrored in
// position and bone direction (+Y) but not in roll: pasting a posed left arm
// flipped onto the right arm gives world transforms mirrored across the
// skeleton root's X = 0 plane - the child heads, and every bone's +Y axis.
TEST(Bone_pose, flipped_paste_mirrors_world_transforms_with_asymmetric_rolls)
{
    // The skeleton root sits at the origin with an identity rest.
    const glm::mat4 root{1.0f};

    // Left arm rests: shoulder at +x, pointing along +x (the bone's +Y turned
    // onto +x), rolled 0.3 rad about its own axis; forearm 0.5 along the
    // shoulder's +Y, rolled 0.8 rad.
    const glm::mat4 roll_l1  = glm::rotate(glm::mat4{1.0f}, 0.3f, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::mat4 roll_l2  = glm::rotate(glm::mat4{1.0f}, 0.8f, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::mat4 to_x     = glm::rotate(glm::mat4{1.0f}, -glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f}); // +Y -> +X
    const glm::mat4 to_neg_x = glm::rotate(glm::mat4{1.0f},  glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f}); // +Y -> -X
    const glm::mat4 rest_l1  = glm::translate(glm::mat4{1.0f}, glm::vec3{ 0.2f, 1.0f, 0.0f}) * to_x * roll_l1;
    const glm::mat4 rest_l2  = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.5f, 0.0f}) * roll_l2;
    // Right arm: mirrored positions and directions, different rolls.
    const glm::mat4 rest_r1  = glm::translate(glm::mat4{1.0f}, glm::vec3{-0.2f, 1.0f, 0.0f}) * to_neg_x * glm::rotate(glm::mat4{1.0f}, -1.1f, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::mat4 rest_r2  = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.5f, 0.0f}) * glm::rotate(glm::mat4{1.0f}, 2.0f, glm::vec3{0.0f, 1.0f, 0.0f});

    // A posed left arm: the shoulder raised and twisted, the elbow bent.
    const glm::mat4 pose_l1 = rest_l1 * trs(glm::vec3{0.0f}, 0.7f, glm::vec3{1.0f, 0.2f, -0.4f});
    const glm::mat4 pose_l2 = rest_l2 * trs(glm::vec3{0.0f}, 1.2f, glm::vec3{0.0f, 0.3f, 1.0f});

    // B(parent) for the shoulders is the root (identity); for the forearms
    // the shoulders' rests.
    const glm::mat4 pose_r1 = editor::mirror_pose_transform(pose_l1, rest_l1, root, rest_r1, root);
    const glm::mat4 pose_r2 = editor::mirror_pose_transform(pose_l2, rest_l2, rest_l1, rest_r2, rest_r1);

    const glm::mat4 world_l1 = root * pose_l1;
    const glm::mat4 world_l2 = world_l1 * pose_l2;
    const glm::mat4 world_r1 = root * pose_r1;
    const glm::mat4 world_r2 = world_r1 * pose_r2;
    const auto mirrored_point = [](const glm::mat4& world, const glm::vec3& local) -> glm::vec3 {
        return glm::vec3{c_mirror_x * world * glm::vec4{local, 1.0f}};
    };
    const auto point = [](const glm::mat4& world, const glm::vec3& local) -> glm::vec3 {
        return glm::vec3{world * glm::vec4{local, 1.0f}};
    };
    EXPECT_TRUE(close(point(world_r2, glm::vec3{0.0f}),             mirrored_point(world_l2, glm::vec3{0.0f})))             << "forearm heads mirror";
    EXPECT_TRUE(close(point(world_r1, glm::vec3{0.0f, 0.5f, 0.0f}), mirrored_point(world_l1, glm::vec3{0.0f, 0.5f, 0.0f}))) << "shoulder +Y axes mirror";
    EXPECT_TRUE(close(point(world_r2, glm::vec3{0.0f, 0.4f, 0.0f}), mirrored_point(world_l2, glm::vec3{0.0f, 0.4f, 0.0f}))) << "forearm +Y axes mirror";

    // The same arms with Blender's per-bone rule (S D S in each bone's own
    // frame) would not mirror, because the rolls differ.
    const glm::mat4 naive_r1 = rest_r1 * c_mirror_x * glm::inverse(rest_l1) * pose_l1 * c_mirror_x;
    EXPECT_FALSE(close(point(root * naive_r1, glm::vec3{0.0f, 0.5f, 0.0f}), mirrored_point(world_l1, glm::vec3{0.0f, 0.5f, 0.0f}), 1.0e-2f))
        << "the rest-frame mirror map is what makes asymmetric rolls mirror";
}

// B(parent): the rests from the skeleton root's child down to the parent;
// the root's parent frame is inverse(rest(root)).
TEST(Bone_pose, parent_rest_in_skeleton_composes_the_rests_below_the_root)
{
    const std::shared_ptr<Node> armature = std::make_shared<erhe::scene::Xform>("Armature");
    const std::shared_ptr<Node> hips     = make_bone("hips",  armature);
    const std::shared_ptr<Node> spine    = make_bone("spine", hips);
    const std::shared_ptr<Node> chest    = make_bone("chest", spine);
    const Trs_transform rest_hips {glm::vec3{0.0f, 1.0f, 0.0f}, glm::angleAxis(0.2f, glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{1.0f}};
    const Trs_transform rest_spine{glm::vec3{0.0f, 0.3f, 0.0f}, glm::angleAxis(0.4f, glm::vec3{0.0f, 0.0f, 1.0f}), glm::vec3{1.0f}};
    set_rest(*hips,  rest_hips);
    set_rest(*spine, rest_spine);

    EXPECT_TRUE(mat_close(editor::get_parent_rest_in_skeleton(hips),  glm::inverse(rest_hips.get_matrix())));
    EXPECT_TRUE(mat_close(editor::get_parent_rest_in_skeleton(spine), glm::mat4{1.0f}));
    EXPECT_TRUE(mat_close(editor::get_parent_rest_in_skeleton(chest), rest_spine.get_matrix()));
}

// R14 plan: rests read from Rig.rest_*, locks respected, lock_edit bones
// sealed, bones already at rest unchanged.
TEST(Bone_pose, clear_plan_uses_the_rest_properties_and_skips_sealed_bones)
{
    const std::shared_ptr<Node> armature = std::make_shared<erhe::scene::Xform>("Armature");
    const std::shared_ptr<Node> hips     = make_bone("hips",  armature);
    const std::shared_ptr<Node> spine    = make_bone("spine", hips);
    const std::shared_ptr<Node> neck     = make_bone("neck",  spine);
    const std::shared_ptr<Node> head     = make_bone("head",  neck);

    const Trs_transform rest{glm::vec3{0.0f, 0.3f, 0.0f}, glm::angleAxis(0.4f, glm::vec3{0.0f, 0.0f, 1.0f}), glm::vec3{1.0f}};
    set_rest(*spine, rest);
    set_rest(*neck,  rest);
    set_rest(*head,  rest);
    spine->set_parent_from_node(Trs_transform{glm::vec3{0.0f, 0.3f, 0.0f}, glm::angleAxis(1.0f, glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{1.0f}});
    neck ->set_parent_from_node(Trs_transform{glm::vec3{0.0f, 0.3f, 0.0f}, glm::angleAxis(1.0f, glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{1.0f}});
    neck ->set_lock_edit(true);
    head ->set_parent_from_node(rest);

    Bone_pose_plan plan;
    const std::vector<std::shared_ptr<Node>> bones{spine, neck, head};
    editor::plan_clear_pose(bones, Pose_channels{.rotation = true}, plan);
    ASSERT_EQ(plan.changes.size(), std::size_t{1}) << "neck is sealed, head is already at rest";
    EXPECT_EQ(plan.changes[0].bone, spine);
    EXPECT_TRUE(quat_close(plan.changes[0].after.get_rotation(), rest.get_rotation()));
    ASSERT_EQ(plan.sealed.size(), std::size_t{1});
    EXPECT_EQ(plan.sealed[0], neck);

    // Every rotation axis locked: nothing to change.
    spine->enable_flag_bits(erhe::Item_flags::lock_rotation_x | erhe::Item_flags::lock_rotation_y | erhe::Item_flags::lock_rotation_z);
    editor::plan_clear_pose(bones, Pose_channels{.rotation = true}, plan);
    EXPECT_TRUE(plan.changes.empty());
}

// R15 plan: normal paste by name, flipped paste onto the counterpart,
// unmatched names reported.
TEST(Bone_pose, paste_plan_matches_names_and_flips_sides)
{
    const std::shared_ptr<Node> armature = std::make_shared<erhe::scene::Xform>("Armature");
    const std::shared_ptr<Node> hips     = make_bone("hips",  armature);
    const std::shared_ptr<Node> arm_l    = make_bone("arm_L", hips);
    const std::shared_ptr<Node> arm_r    = make_bone("arm_R", hips);
    // Mirror-image rests, so the flipped paste is S P S.
    const glm::mat4 rest_l = trs(glm::vec3{0.2f, 0.5f, 0.0f}, 0.3f, glm::vec3{0.0f, 0.0f, 1.0f});
    set_rest(*arm_l, Trs_transform{rest_l});
    set_rest(*arm_r, Trs_transform{c_mirror_x * rest_l * c_mirror_x});
    arm_l->set_parent_from_node(Trs_transform{rest_l});
    arm_r->set_parent_from_node(Trs_transform{c_mirror_x * rest_l * c_mirror_x});

    const glm::quat pose_rotation = glm::normalize(glm::quat{0.8f, 0.3f, 0.4f, -0.2f});
    Bone_pose pose;
    pose.bones.push_back(editor::Bone_pose_entry{.name = "arm_L", .translation = glm::vec3{0.2f, 0.5f, 0.1f}, .rotation = pose_rotation});
    pose.bones.push_back(editor::Bone_pose_entry{.name = "tail",  .translation = glm::vec3{0.0f}});

    Bone_pose_plan plan;
    editor::plan_paste_pose(arm_r, pose, Paste_pose_mode::normal, plan);
    ASSERT_EQ(plan.changes.size(), std::size_t{1});
    EXPECT_EQ(plan.changes[0].bone, arm_l);
    EXPECT_TRUE(quat_close(plan.changes[0].after.get_rotation(), pose_rotation));
    ASSERT_EQ(plan.unmatched.size(), std::size_t{1});
    EXPECT_EQ(plan.unmatched[0], "tail");

    editor::plan_paste_pose(arm_l, pose, Paste_pose_mode::flipped, plan);
    ASSERT_EQ(plan.changes.size(), std::size_t{1});
    EXPECT_EQ(plan.changes[0].bone, arm_r);
    EXPECT_TRUE(close(plan.changes[0].after.get_translation(), glm::vec3{-0.2f, 0.5f, 0.1f}));
    EXPECT_TRUE(quat_close(plan.changes[0].after.get_rotation(), glm::quat{pose_rotation.w, pose_rotation.x, -pose_rotation.y, -pose_rotation.z}));
    EXPECT_EQ(plan.unmatched.size(), std::size_t{1});
}

// Copy Pose records names and local TRS.
TEST(Bone_pose, copy_records_names_and_local_transforms)
{
    const std::shared_ptr<Node> hips  = make_bone("hips", {});
    const std::shared_ptr<Node> spine = make_bone("spine", hips);
    spine->set_parent_from_node(Trs_transform{glm::vec3{0.0f, 0.3f, 0.0f}, glm::angleAxis(0.4f, glm::vec3{0.0f, 0.0f, 1.0f}), glm::vec3{2.0f}});
    const std::vector<std::shared_ptr<Node>> bones{spine, hips};
    const Bone_pose pose = editor::copy_bone_pose(bones);
    ASSERT_EQ(pose.bones.size(), std::size_t{2});
    EXPECT_EQ(pose.bones[0].name, "spine");
    EXPECT_TRUE(close(pose.bones[0].translation, glm::vec3{0.0f, 0.3f, 0.0f}));
    EXPECT_TRUE(quat_close(pose.bones[0].rotation, glm::angleAxis(0.4f, glm::vec3{0.0f, 0.0f, 1.0f})));
    EXPECT_TRUE(close(pose.bones[0].scale, glm::vec3{2.0f}));
    EXPECT_EQ(pose.bones[1].name, "hips");
}

} // anonymous namespace
