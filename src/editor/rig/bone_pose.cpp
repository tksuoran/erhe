#include "rig/bone_pose.hpp"
#include "rig/bone_hierarchy.hpp"
#include "rig/bone_naming.hpp"

#include "scene/rig_properties.hpp"
#include "transform/channel_locks.hpp"

#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"

#include <algorithm>

namespace editor {

auto clear_pose_channels(
    const erhe::scene::Trs_transform& current,
    const erhe::scene::Trs_transform& rest,
    const Pose_channels&              channels,
    const uint64_t                    lock_flags
) -> erhe::scene::Trs_transform
{
    const erhe::scene::Trs_transform cleared{
        channels.location ? rest.get_translation() : current.get_translation(),
        channels.rotation ? rest.get_rotation()    : current.get_rotation(),
        channels.scale    ? rest.get_scale()       : current.get_scale()
    };
    return apply_channel_locks(current, cleared, lock_flags);
}

auto mirror_pose_transform(
    const glm::mat4& source_pose,
    const glm::mat4& source_rest,
    const glm::mat4& source_parent_rest_in_skeleton,
    const glm::mat4& target_rest,
    const glm::mat4& target_parent_rest_in_skeleton
) -> glm::mat4
{
    const glm::mat4 mirror_x{
        glm::vec4{-1.0f, 0.0f, 0.0f, 0.0f},
        glm::vec4{ 0.0f, 1.0f, 0.0f, 0.0f},
        glm::vec4{ 0.0f, 0.0f, 1.0f, 0.0f},
        glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f}
    };
    const glm::mat4 source_parent_from_target_parent = glm::inverse(source_parent_rest_in_skeleton) * mirror_x * target_parent_rest_in_skeleton;
    const glm::mat4 change_from_rest                 = source_pose * glm::inverse(source_rest);
    return glm::inverse(source_parent_from_target_parent) * change_from_rest * source_parent_from_target_parent * target_rest;
}

auto get_parent_rest_in_skeleton(const std::shared_ptr<erhe::scene::Node>& bone) -> glm::mat4
{
    const std::shared_ptr<erhe::scene::Node> root = get_skeleton_root(bone);
    if (!root) {
        return glm::mat4{1.0f};
    }
    if (root == bone) {
        return glm::inverse(read_rest_transform(*root).get_matrix());
    }
    // B(parent) = rest(child of root) * ... * rest(parent): walk from the
    // parent up to (not including) the root, composing on the left.
    glm::mat4 parent_rest_in_skeleton{1.0f};
    for (std::shared_ptr<erhe::scene::Node> node = bone->get_parent_node(); node && (node != root); node = node->get_parent_node()) {
        parent_rest_in_skeleton = read_rest_transform(*node).get_matrix() * parent_rest_in_skeleton;
    }
    return parent_rest_in_skeleton;
}

auto copy_bone_pose(const std::span<const std::shared_ptr<erhe::scene::Node>> bones) -> Bone_pose
{
    Bone_pose pose;
    pose.bones.reserve(bones.size());
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        if (!bone) {
            continue;
        }
        const erhe::scene::Trs_transform& local = bone->parent_from_node_transform();
        pose.bones.push_back(
            Bone_pose_entry{
                .name        = bone->get_name(),
                .translation = local.get_translation(),
                .rotation    = local.get_rotation(),
                .scale       = local.get_scale()
            }
        );
    }
    return pose;
}

namespace {

// Adds the change of `bone` to `after` (channel locks applied), replacing an
// earlier change of the same bone; lock_edit bones go to `sealed`.
void add_change(const std::shared_ptr<erhe::scene::Node>& bone, const erhe::scene::Trs_transform& candidate, Bone_pose_plan& out)
{
    const auto listed = std::find_if(
        out.changes.begin(), out.changes.end(),
        [&bone](const Bone_pose_change& change) { return change.bone == bone; }
    );
    if (listed != out.changes.end()) {
        out.changes.erase(listed);
    }
    if (bone->is_lock_edit()) {
        if (std::find(out.sealed.begin(), out.sealed.end(), bone) == out.sealed.end()) {
            out.sealed.push_back(bone);
        }
        return;
    }
    const erhe::scene::Trs_transform& before = bone->parent_from_node_transform();
    const erhe::scene::Trs_transform  after  = apply_channel_locks(before, candidate, bone->get_flag_bits());
    if (after.get_matrix() == before.get_matrix()) {
        return;
    }
    out.changes.push_back(Bone_pose_change{.bone = bone, .before = before, .after = after});
}

} // anonymous namespace

void plan_clear_pose(
    const std::span<const std::shared_ptr<erhe::scene::Node>> bones,
    const Pose_channels&                                      channels,
    Bone_pose_plan&                                           out
)
{
    out.changes.clear();
    out.sealed.clear();
    out.unmatched.clear();
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        if (!bone || !erhe::scene::is_bone(bone.get())) {
            continue;
        }
        const erhe::scene::Trs_transform& current = bone->parent_from_node_transform();
        const erhe::scene::Trs_transform  rest    = read_rest_transform(*bone);
        // Locks are applied by add_change, against the same `current`.
        add_change(bone, clear_pose_channels(current, rest, channels, 0), out);
    }
}

void plan_paste_pose(
    const std::shared_ptr<erhe::scene::Node>& skeleton_bone,
    const Bone_pose&                          pose,
    const Paste_pose_mode                     mode,
    Bone_pose_plan&                           out
)
{
    out.changes.clear();
    out.sealed.clear();
    out.unmatched.clear();
    const std::shared_ptr<erhe::scene::Node> root = get_skeleton_root(skeleton_bone);
    for (const Bone_pose_entry& entry : pose.bones) {
        const erhe::scene::Trs_transform entry_pose{entry.translation, entry.rotation, entry.scale};
        if (mode == Paste_pose_mode::normal) {
            const std::shared_ptr<erhe::scene::Node> target = find_skeleton_bone(root, entry.name);
            if (!target) {
                out.unmatched.push_back(entry.name);
                continue;
            }
            add_change(target, entry_pose, out);
            continue;
        }
        const std::string target_name = (bone_side(entry.name) == Bone_side::none) ? entry.name : flip_side_name(entry.name);
        const std::shared_ptr<erhe::scene::Node> target = find_skeleton_bone(root, target_name);
        const std::shared_ptr<erhe::scene::Node> source = find_skeleton_bone(root, entry.name);
        if (!target || !source) {
            out.unmatched.push_back(entry.name);
            continue;
        }
        const glm::mat4 mirrored = mirror_pose_transform(
            entry_pose.get_matrix(),
            read_rest_transform(*source).get_matrix(),
            get_parent_rest_in_skeleton(source),
            read_rest_transform(*target).get_matrix(),
            get_parent_rest_in_skeleton(target)
        );
        add_change(target, erhe::scene::Trs_transform{mirrored}, out);
    }
}

}
