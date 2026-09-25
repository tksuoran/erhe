#pragma once

#include "rig/bone_hierarchy.hpp"
#include "rig/bone_pose.hpp"

#include <memory>
#include <string>
#include <vector>

namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace editor {

class App_context;

// The skeleton editing verbs of doc/plans/rigging/skeleton_editing.md slice A
// (R10, R13 Flip Names) and slice B (R14 Clear, R15 Paste Pose), shared by
// the Hierarchy context menu and the MCP tools. One-shot commands: nothing
// here runs per frame.

// The bones a Hierarchy context-menu verb acts on: the selected bones when
// the clicked node is selected (the clicked node first), otherwise the
// clicked node alone. Non-bone nodes are left out.
[[nodiscard]] auto get_bone_command_targets(App_context& context, const std::shared_ptr<erhe::scene::Node>& clicked_node) -> std::vector<std::shared_ptr<erhe::scene::Node>>;

// R10: replaces the selection within the targets' scene by the bones `mode`
// selects from `targets` (collect_bone_selection) and makes the first of them
// active - for Bone_select_mode::chain the first target itself, which is on
// its chain. Selection changes are not undoable. When `mode` selects nothing
// the selection is left unchanged. Returns the bones selected.
auto select_bones(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets,
    Bone_select_mode                                       mode
) -> std::vector<std::shared_ptr<erhe::scene::Node>>;

class Bone_rename
{
public:
    std::shared_ptr<erhe::scene::Node> bone;
    std::string                        from;
    std::string                        to;
};

class Bone_rename_skip
{
public:
    std::shared_ptr<erhe::scene::Node> bone;
    std::string                        reason;
};

class Flip_bone_names_result
{
public:
    std::vector<Bone_rename>      renamed;
    std::vector<Bone_rename_skip> skipped;
};

// R13 Flip Names: renames each target bone to flip_side_name(its name) as
// one undoable operation (Property_set_operation on Item_base::name_property,
// the path the Properties window Name row and set_item_property use). Two
// sibling targets whose names flip into each other (arm_L and arm_R under one
// parent) swap names through a temporary name inside the same operation. A
// bone whose name has no side is skipped; a bone whose flipped name a sibling
// outside the targets already holds is skipped with a logged warning. Queues
// nothing when nothing is renamed.
auto flip_bone_names(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets
) -> Flip_bone_names_result;

// R14 Clear Location / Rotation / Scale / All: sets `channels` of the target
// bones to their rest transforms (plan_clear_pose: channel locks kept,
// lock_edit bones skipped) as one undoable operation - a
// Node_transform_operation per changed bone in one Compound_operation.
// Queues nothing when nothing changes. Returns the plan it applied.
auto clear_bone_pose(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets,
    const Pose_channels&                                   channels
) -> Bone_pose_plan;

// R15 Paste Pose / Paste Pose Flipped: writes `pose` onto the skeleton
// `skeleton_bone` is on (plan_paste_pose) as one undoable operation, as
// clear_bone_pose does. Unmatched names are logged and returned in the plan,
// not an error.
auto paste_bone_pose(
    App_context&                              context,
    const std::shared_ptr<erhe::scene::Node>& skeleton_bone,
    const Bone_pose&                          pose,
    Paste_pose_mode                           mode
) -> Bone_pose_plan;

// The menu / tool label of a Clear verb's channels ("Clear Location" ..
// "Clear All").
[[nodiscard]] auto get_clear_pose_label(const Pose_channels& channels) -> const char*;

// The menu / tool label of a mode.
[[nodiscard]] auto get_bone_select_mode_label(Bone_select_mode mode) -> const char*;

}
