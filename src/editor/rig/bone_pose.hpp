#pragma once

#include "erhe_scene/trs_transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace editor {

// The posing verbs of doc/plans/rigging/skeleton_editing.md slice B: Clear
// Location / Rotation / Scale / All (R14) and Copy Pose / Paste Pose / Paste
// Pose Flipped (R15). Everything here is a pure function or a read of the
// scene graph; the undoable commands that apply the results are in
// rig/bone_commands.hpp.

// The TRS channels a Clear verb resets.
class Pose_channels
{
public:
    bool location{false};
    bool rotation{false};
    bool scale   {false};
};

// R14: `current` with the chosen channels replaced by those of `rest`, the
// channels `lock_flags` locks (Item_flags::lock_translation_x ..
// lock_scale_z, per axis, the Transform tool's rule: apply_channel_locks)
// kept at their `current` values.
[[nodiscard]] auto clear_pose_channels(
    const erhe::scene::Trs_transform& current,
    const erhe::scene::Trs_transform& rest,
    const Pose_channels&              channels,
    uint64_t                          lock_flags
) -> erhe::scene::Trs_transform;

// R15 Paste Pose Flipped, the mirror of one bone's pose:
//
//   S       = diag(-1, 1, 1): the reflection across the X = 0 plane of the
//             skeleton root's rest frame
//   B(b)    = the rest transform of bone b relative to the skeleton root's
//             rest frame: the product of the Rig.rest_* local transforms from
//             the root's child down to b (identity for the root itself; the
//             root's parent frame is inverse(rest(root)))
//   G       = inverse(B(source parent)) * S * B(target parent): the rest-pose
//             mirror map from the target's parent frame to the source's
//   D       = source_pose * inverse(source_rest): the source's change from
//             its rest, in its parent's frame
//   result  = inverse(G) * D * G * target_rest
//
// A pose equal to the source's rest gives the target's rest; mirroring the
// result back gives the source pose (the map is an involution). When the
// rest frames are mirror images across S (B(target) = S * B(source) * S at
// every level, the convention of symmetric Blender rigs) it is
// S * source_pose * S: translation x negated, quaternion (w, x, y, z) ->
// (w, x, -y, -z). With rest frames whose rolls differ between the sides it
// still gives world transforms mirrored across the skeleton root's X = 0
// plane (up to the asymmetry of the rest pose itself), because the change is
// carried through the mirror of the parents' rest frames rather than
// through each bone's own axes. The matrices are the bones' local
// (parent-from-node) transforms and the rest frames above.
[[nodiscard]] auto mirror_pose_transform(
    const glm::mat4& source_pose,
    const glm::mat4& source_rest,
    const glm::mat4& source_parent_rest_in_skeleton,
    const glm::mat4& target_rest,
    const glm::mat4& target_parent_rest_in_skeleton
) -> glm::mat4;

// B(parent of bone) of mirror_pose_transform: the rest frame of the bone's
// parent relative to the skeleton root's rest frame (inverse(rest(root)) for
// the root itself). Reads the Rig.rest_* values of the bones from `bone` up to
// its skeleton root (rig/bone_hierarchy.hpp get_skeleton_root).
[[nodiscard]] auto get_parent_rest_in_skeleton(const std::shared_ptr<erhe::scene::Node>& bone) -> glm::mat4;

// One bone of a pose buffer: its name and local TRS.
class Bone_pose_entry
{
public:
    std::string name;
    glm::vec3   translation{0.0f};
    glm::quat   rotation   {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3   scale      {1.0f};
};

// A copied pose (R15): bone names and local TRS only, so it pastes onto any
// skeleton with bones of those names.
class Bone_pose
{
public:
    std::vector<Bone_pose_entry> bones;
};

enum class Paste_pose_mode : unsigned int {
    normal  = 0, // each entry onto the bone of its name
    flipped = 1  // each entry onto the bone of its side-flipped name, mirrored
};

// One planned transform change of a posing verb.
class Bone_pose_change
{
public:
    std::shared_ptr<erhe::scene::Node> bone;
    erhe::scene::Trs_transform         before;
    erhe::scene::Trs_transform         after;
};

class Bone_pose_plan
{
public:
    std::vector<Bone_pose_change>                   changes;
    // Bones left alone because they carry Item_flags::lock_edit.
    std::vector<std::shared_ptr<erhe::scene::Node>> sealed;
    // Paste: pose entries whose target bone (or, flipped, whose own-name rest
    // source) is not in the skeleton.
    std::vector<std::string>                        unmatched;
};

// Copy Pose: the bones' names and current local TRS, in order.
[[nodiscard]] auto copy_bone_pose(std::span<const std::shared_ptr<erhe::scene::Node>> bones) -> Bone_pose;

// R14: the changes that clear `channels` of `bones` to their rest transforms
// (rig properties read_rest_transform). Bones with lock_edit are listed in
// `sealed`; bones the clear leaves unchanged (already at rest, or every
// chosen channel locked) contribute no change.
void plan_clear_pose(
    std::span<const std::shared_ptr<erhe::scene::Node>> bones,
    const Pose_channels&                                channels,
    Bone_pose_plan&                                     out
);

// R15: the changes that paste `pose` onto the skeleton `skeleton_bone` is on
// (the bone of the entry's name, or of its flipped name mirrored by
// mirror_pose_transform; a name without a side flips onto itself), channel
// locks respected. Unchanged bones contribute no change; for two entries
// naming the same bone the later wins.
void plan_paste_pose(
    const std::shared_ptr<erhe::scene::Node>& skeleton_bone,
    const Bone_pose&                          pose,
    Paste_pose_mode                           mode,
    Bone_pose_plan&                           out
);

}
