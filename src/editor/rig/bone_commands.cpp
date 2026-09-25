#include "rig/bone_commands.hpp"
#include "rig/bone_naming.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "animation/animation_player.hpp"
#include "operations/compound_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace editor {

auto get_bone_command_targets(App_context& context, const std::shared_ptr<erhe::scene::Node>& clicked_node) -> std::vector<std::shared_ptr<erhe::scene::Node>>
{
    std::vector<std::shared_ptr<erhe::scene::Node>> targets;
    if (!clicked_node) {
        return targets;
    }
    if (erhe::scene::is_bone(clicked_node.get())) {
        targets.push_back(clicked_node);
    }
    if (clicked_node->is_selected() && (context.selection != nullptr)) {
        for (const std::shared_ptr<erhe::Item_base>& item : context.selection->get_selected_items()) {
            std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if (!node || !erhe::scene::is_bone(node.get())) {
                continue;
            }
            if (std::find(targets.begin(), targets.end(), node) == targets.end()) {
                targets.push_back(std::move(node));
            }
        }
    }
    return targets;
}

auto get_bone_select_mode_label(const Bone_select_mode mode) -> const char*
{
    switch (mode) {
        case Bone_select_mode::parent:             return "Select Parent";
        case Bone_select_mode::children:           return "Select Children";
        case Bone_select_mode::children_recursive: return "Select Children (All)";
        case Bone_select_mode::chain:              return "Select Chain";
        case Bone_select_mode::mirror:             return "Select Mirror";
        default:                                   return "?";
    }
}

auto select_bones(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets,
    const Bone_select_mode                                 mode
) -> std::vector<std::shared_ptr<erhe::scene::Node>>
{
    std::vector<std::shared_ptr<erhe::scene::Node>> result;
    collect_bone_selection(targets, mode, result);
    if (result.empty() || (context.selection == nullptr)) {
        log_selection->info("{}: nothing to select from {} bone(s); selection unchanged", get_bone_select_mode_label(mode), targets.size());
        return result;
    }

    const std::shared_ptr<erhe::scene::Node> active = (mode == Bone_select_mode::chain) ? targets.front() : result.front();
    Selection& selection = *context.selection;
    {
        Scoped_selection_change selection_change{selection};
        selection.clear_selection(result.front()->get_item_host());
        for (const std::shared_ptr<erhe::scene::Node>& bone : result) {
            selection.add_to_selection(bone);
        }
        selection.set_active_item(active);
    }
    log_selection->info("{}: selected {} bone(s)", get_bone_select_mode_label(mode), result.size());
    return result;
}

namespace {

// The sibling of `bone` (another child of its parent) named `name`; null when
// there is none or `bone` has no parent.
[[nodiscard]] auto find_sibling_named(const erhe::scene::Node& bone, const std::string& name) -> std::shared_ptr<erhe::Hierarchy>
{
    const std::shared_ptr<erhe::Hierarchy> parent = bone.get_parent().lock();
    if (!parent) {
        return {};
    }
    for (const std::shared_ptr<erhe::Hierarchy>& child : parent->get_children()) {
        if ((child.get() != &bone) && (child->get_name() == name)) {
            return child;
        }
    }
    return {};
}

[[nodiscard]] auto make_rename_operation(const std::shared_ptr<erhe::scene::Node>& bone, const std::string& from, const std::string& to) -> std::shared_ptr<Operation>
{
    return std::make_shared<Property_set_operation>(
        bone,
        erhe::Item_base::name_property.get(),
        std::optional<erhe::property::Property_value>{erhe::property::Property_value{from}},
        std::optional<erhe::property::Property_value>{erhe::property::Property_value{to}}
    );
}

} // anonymous namespace

auto flip_bone_names(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets
) -> Flip_bone_names_result
{
    Flip_bone_names_result result;

    // The renames, deduplicated, bones without a side skipped.
    std::vector<Bone_rename> renames;
    for (const std::shared_ptr<erhe::scene::Node>& bone : targets) {
        if (!bone || !erhe::scene::is_bone(bone.get())) {
            continue;
        }
        const bool listed = std::any_of(
            renames.begin(), renames.end(),
            [&bone](const Bone_rename& rename) { return rename.bone == bone; }
        );
        if (listed) {
            continue;
        }
        const std::string& name = bone->get_name();
        if (bone_side(name) == Bone_side::none) {
            result.skipped.push_back(Bone_rename_skip{.bone = bone, .reason = "name has no side"});
            continue;
        }
        renames.push_back(Bone_rename{.bone = bone, .from = name, .to = flip_side_name(name)});
    }

    // A flipped name a sibling holds: when that sibling is renamed too (its
    // own name flips into this one, as flip_side_name is an involution) the
    // two swap through a temporary name; otherwise the bone is skipped, as
    // names are sibling-unique (doc/erhe/usd_compatibility_design.md M2).
    const auto is_renamed = [&renames](const erhe::Hierarchy* item) -> bool {
        return std::any_of(
            renames.begin(), renames.end(),
            [item](const Bone_rename& rename) { return rename.bone.get() == item; }
        );
    };
    std::vector<Bone_rename> accepted;
    std::vector<bool>        swapped;
    for (const Bone_rename& rename : renames) {
        const std::shared_ptr<erhe::Hierarchy> holder = find_sibling_named(*rename.bone, rename.to);
        if (holder && !is_renamed(holder.get())) {
            log_operations->warn(
                "Flip Names: '{}' not renamed to '{}' - a sibling already has that name",
                rename.from, rename.to
            );
            result.skipped.push_back(Bone_rename_skip{.bone = rename.bone, .reason = "a sibling already has the flipped name"});
            continue;
        }
        accepted.push_back(rename);
        swapped.push_back(static_cast<bool>(holder));
    }
    if (accepted.empty()) {
        return result;
    }

    // Swapped bones step aside to a temporary name first, so every final
    // rename lands on a free name; undo runs the steps in reverse.
    Compound_operation::Parameters parameters;
    std::vector<std::string> current_names;
    current_names.reserve(accepted.size());
    for (std::size_t i = 0, end = accepted.size(); i < end; ++i) {
        const Bone_rename& rename = accepted[i];
        if (swapped[i]) {
            const std::string temporary = fmt::format("{} (flipping {})", rename.from, rename.bone->get_id());
            parameters.operations.push_back(make_rename_operation(rename.bone, rename.from, temporary));
            current_names.push_back(temporary);
        } else {
            current_names.push_back(rename.from);
        }
    }
    for (std::size_t i = 0, end = accepted.size(); i < end; ++i) {
        const Bone_rename& rename = accepted[i];
        parameters.operations.push_back(make_rename_operation(rename.bone, current_names[i], rename.to));
        log_operations->info("Flip Names: '{}' -> '{}'", rename.from, rename.to);
    }
    if (parameters.operations.size() == 1) {
        context.operation_stack->queue(parameters.operations.front());
    } else {
        context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
    }
    result.renamed = std::move(accepted);
    return result;
}

namespace {

// One undo step for the plan's changes: a Node_transform_operation per bone
// in one Compound_operation.
void queue_pose_plan(App_context& context, const Bone_pose_plan& plan)
{
    if (plan.changes.empty()) {
        return;
    }
    Compound_operation::Parameters parameters;
    for (const Bone_pose_change& change : plan.changes) {
        parameters.operations.push_back(
            std::make_shared<Node_transform_operation>(
                Node_transform_operation::Parameters{
                    .node                    = change.bone,
                    .parent_from_node_before = change.before,
                    .parent_from_node_after  = change.after,
                    .xform_op_stack_before   = change.bone->copy_xform_op_stack(),
                    .time_duration           = 0.0f
                }
            )
        );
    }
    context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
}

void log_sealed(const char* const verb, const Bone_pose_plan& plan)
{
    for (const std::shared_ptr<erhe::scene::Node>& bone : plan.sealed) {
        log_operations->info("{}: '{}' skipped - it is locked (lock_edit)", verb, bone->get_name());
    }
}

} // anonymous namespace

auto get_clear_pose_label(const Pose_channels& channels) -> const char*
{
    if (channels.location && channels.rotation && channels.scale) {
        return "Clear All";
    }
    if (channels.location && !channels.rotation && !channels.scale) {
        return "Clear Location";
    }
    if (!channels.location && channels.rotation && !channels.scale) {
        return "Clear Rotation";
    }
    if (!channels.location && !channels.rotation && channels.scale) {
        return "Clear Scale";
    }
    return "Clear Pose";
}

auto clear_bone_pose(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets,
    const Pose_channels&                                   channels
) -> Bone_pose_plan
{
    // As Reset Bones to Bind Pose: an animation playing on the bones would
    // hide the cleared transforms, and the current poses read below must be
    // the authored ones.
    if (context.animation_player != nullptr) {
        static_cast<void>(context.animation_player->stop_if_targeting(
            [&targets](const erhe::scene::Node& node) {
                return std::any_of(
                    targets.begin(), targets.end(),
                    [&node](const std::shared_ptr<erhe::scene::Node>& target) { return target.get() == &node; }
                );
            }
        ));
    }

    Bone_pose_plan plan;
    plan_clear_pose(targets, channels, plan);
    const char* const verb = get_clear_pose_label(channels);
    log_sealed(verb, plan);
    log_operations->info("{}: {} of {} bone(s) change", verb, plan.changes.size(), targets.size());
    queue_pose_plan(context, plan);
    return plan;
}

auto paste_bone_pose(
    App_context&                              context,
    const std::shared_ptr<erhe::scene::Node>& skeleton_bone,
    const Bone_pose&                          pose,
    const Paste_pose_mode                     mode
) -> Bone_pose_plan
{
    // As clear_bone_pose, for the bones of the target skeleton.
    const std::shared_ptr<erhe::scene::Node> skeleton_root = get_skeleton_root(skeleton_bone);
    if ((context.animation_player != nullptr) && skeleton_root) {
        static_cast<void>(context.animation_player->stop_if_targeting(
            [&skeleton_root](const erhe::scene::Node& node) {
                if (!erhe::scene::is_bone(&node)) {
                    return false;
                }
                const erhe::scene::Node* root = &node;
                for (std::shared_ptr<erhe::scene::Node> parent = root->get_parent_node(); parent && erhe::scene::is_bone(parent.get()); parent = parent->get_parent_node()) {
                    root = parent.get();
                }
                return root == skeleton_root.get();
            }
        ));
    }

    Bone_pose_plan plan;
    plan_paste_pose(skeleton_bone, pose, mode, plan);
    const char* const verb = (mode == Paste_pose_mode::flipped) ? "Paste Pose Flipped" : "Paste Pose";
    log_sealed(verb, plan);
    for (const std::string& name : plan.unmatched) {
        log_operations->info("{}: no bone for '{}' in the skeleton; entry not pasted", verb, name);
    }
    log_operations->info("{}: {} of {} pose entries change a bone", verb, plan.changes.size(), pose.bones.size());
    queue_pose_plan(context, plan);
    return plan;
}

}
