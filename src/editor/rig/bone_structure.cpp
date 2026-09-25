#include "rig/bone_structure.hpp"
#include "rig/bone_hierarchy.hpp"
#include "rig/bone_mirror.hpp"
#include "rig/bone_naming.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"
#include "scene/ik_properties.hpp"
#include "scene/rig_properties.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_property/expression.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace editor {

auto get_bound_bone_refusal(const erhe::scene::Node& bone) -> std::optional<std::string>
{
    const std::optional<erhe::scene::Skin_joint> skin_joint = erhe::scene::find_skin_joint(bone);
    if (!skin_joint.has_value()) {
        return std::nullopt;
    }
    return fmt::format(
        "'{}' is a joint of skin '{}': the structure and rest pose of a bound skeleton are fixed by its bind (skeleton_editing.md R9)",
        bone.get_name(), skin_joint.value().skin->get_name()
    );
}

auto get_bound_bones_refusal(const std::vector<std::shared_ptr<erhe::scene::Node>>& bones) -> std::optional<std::string>
{
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        if (!bone) {
            continue;
        }
        std::optional<std::string> refusal = get_bound_bone_refusal(*bone);
        if (refusal.has_value()) {
            return refusal;
        }
    }
    return std::nullopt;
}

auto get_bone_delete_mode_label(const Bone_delete_mode mode) -> const char*
{
    switch (mode) {
        case Bone_delete_mode::delete_bones: return "Delete Bone";
        case Bone_delete_mode::dissolve:     return "Dissolve Bone";
        default:                             return "?";
    }
}

namespace {

using erhe::property::Local_state;
using erhe::property::Property_value;

// The selection a creating verb leaves: its execute selects the new bones
// (the first one active), its undo puts back the selection it was built
// over. Last in its compound, so it runs after the inserts on execute and
// first on undo.
class Bone_selection_operation : public Operation
{
public:
    Bone_selection_operation(Selection& selection, std::vector<std::shared_ptr<erhe::Item_base>> after)
        : m_before       {selection.get_selected_items()}
        , m_active_before{selection.get_active_item()}
        , m_after        {std::move(after)}
    {
        m_active_after = m_after.empty() ? std::shared_ptr<erhe::Item_base>{} : m_after.front();
        set_description(fmt::format("[{}] Bone_selection {} bone(s)", get_serial(), m_after.size()));
    }

    void execute(App_context& context) override
    {
        if (context.selection != nullptr) {
            context.selection->set_selection(m_after, m_active_after);
        }
    }

    void undo(App_context& context) override
    {
        if (context.selection != nullptr) {
            context.selection->set_selection(m_before, m_active_before.lock());
        }
    }

private:
    std::vector<std::shared_ptr<erhe::Item_base>> m_before;
    std::weak_ptr<erhe::Item_base>                m_active_before;
    std::vector<std::shared_ptr<erhe::Item_base>> m_after;
    std::shared_ptr<erhe::Item_base>              m_active_after;
};

// A transform operation that writes `transform` as the node's local
// transform on execute and `before` on undo. With before == after it pins a
// local transform: Xformable::set_parent preserves the WORLD transform, so a
// re-parent recomputes the local one (with rounding); a pin placed before
// the re-parent in a compound makes its undo exact, one placed after it
// makes the result exact.
[[nodiscard]] auto make_pin(const std::shared_ptr<erhe::scene::Node>& node, const erhe::scene::Trs_transform& transform) -> std::shared_ptr<Operation>
{
    return std::make_shared<Node_transform_operation>(
        Node_transform_operation::Parameters{
            .node                    = node,
            .parent_from_node_before = transform,
            .parent_from_node_after  = transform,
            .xform_op_stack_before   = node->copy_xform_op_stack(),
            .time_duration           = 0.0f
        }
    );
}

[[nodiscard]] auto make_property_set(
    const std::shared_ptr<erhe::scene::Node>&  node,
    const erhe::property::Dependency_property& property,
    std::optional<Local_state>                 before,
    std::optional<Local_state>                 after
) -> std::shared_ptr<Operation>
{
    return std::make_shared<Property_set_operation>(node, property, std::move(before), std::move(after));
}

[[nodiscard]] auto is_bone_node(const std::shared_ptr<erhe::Hierarchy>& item) -> std::shared_ptr<erhe::scene::Node>
{
    std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
    if (!node || !erhe::scene::is_bone(node.get())) {
        return {};
    }
    return node;
}

// Names a verb must not reuse: the bones of `bone`'s skeleton and the
// children of `parent`, plus the names the verb already chose.
class Bone_names
{
public:
    void add_skeleton(const std::shared_ptr<erhe::scene::Node>& bone)
    {
        const std::shared_ptr<erhe::scene::Node> root = get_skeleton_root(bone);
        if (!root) {
            return;
        }
        add_bone_subtree(*root);
    }

    void add_children(const erhe::Hierarchy& parent)
    {
        for (const std::shared_ptr<erhe::Hierarchy>& child : parent.get_children()) {
            m_taken.insert(child->get_name());
        }
    }

    [[nodiscard]] auto is_taken(const std::string& name) const -> bool
    {
        return m_taken.contains(name);
    }

    void take(const std::string& name)
    {
        m_taken.insert(name);
    }

    // `wanted` itself when it is free, else the first free `<base>.NNN`.
    [[nodiscard]] auto make(const std::string& wanted) -> std::string
    {
        if (!is_taken(wanted)) {
            take(wanted);
            return wanted;
        }
        return make_indexed(wanted);
    }

    // The first free `<base>.NNN` counting from 1; `base` is `source` without
    // a trailing `.<digits>` index group.
    [[nodiscard]] auto make_indexed(const std::string& source) -> std::string
    {
        std::string base = source;
        const std::size_t dot = base.find_last_of('.');
        if ((dot != std::string::npos) && (dot + 1 < base.size()) && (dot > 0)) {
            const bool digits = std::all_of(base.begin() + static_cast<std::ptrdiff_t>(dot + 1), base.end(), [](const char c) { return (c >= '0') && (c <= '9'); });
            if (digits) {
                base.resize(dot);
            }
        }
        for (std::size_t index = 1; ; ++index) {
            std::string candidate = fmt::format("{}.{:03}", base, index);
            if (!is_taken(candidate)) {
                take(candidate);
                return candidate;
            }
        }
    }

private:
    void add_bone_subtree(const erhe::scene::Node& bone)
    {
        m_taken.insert(bone.get_name());
        for (const std::shared_ptr<erhe::Hierarchy>& child : bone.get_children()) {
            const std::shared_ptr<erhe::scene::Node> child_bone = is_bone_node(child);
            if (child_bone) {
                add_bone_subtree(*child_bone);
            }
        }
    }

    std::unordered_set<std::string> m_taken;
};

class New_bone
{
public:
    std::string                               name;
    std::shared_ptr<erhe::scene::Node>        parent;
    glm::vec3                                 head     {0.0f};
    glm::vec3                                 tail     {0.0f, 1.0f, 0.0f};
    bool                                      connected{false};
    glm::quat                                 rotation {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3                                 scale    {1.0f};
    // The rest transform; the creation local TRS (head, rotation, scale)
    // when absent.
    std::optional<erhe::scene::Trs_transform> rest;
};

// A new bone node carrying the bone flag and its Rig values (tail, connected,
// rest = the creation local TRS), inserted as the last child of its parent
// and pinned to its local transform: the insert preserves the orphan's world
// transform, so the local one is written after it (the add_bone_tip_nodes
// pattern). The values live on the node itself, so undo (removal) and redo
// (re-insert) carry them along.
[[nodiscard]] auto append_new_bone(App_context& context, const New_bone& spec, std::vector<std::shared_ptr<Operation>>& operations) -> std::shared_ptr<erhe::scene::Node>
{
    std::shared_ptr<erhe::scene::Xform> bone = std::make_shared<erhe::scene::Xform>(spec.name);
    bone->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::bone);
    const erhe::scene::Trs_transform local{spec.head, spec.rotation, spec.scale};
    const erhe::scene::Trs_transform rest = spec.rest.has_value() ? spec.rest.value() : local;
    static_cast<void>(bone->set_value(Rig::tail_property(),             spec.tail));
    static_cast<void>(bone->set_value(Rig::rest_translation_property(), rest.get_translation()));
    static_cast<void>(bone->set_value(Rig::rest_rotation_property(),    rest.get_rotation()));
    static_cast<void>(bone->set_value(Rig::rest_scale_property(),       rest.get_scale()));
    if (spec.connected) {
        static_cast<void>(bone->set_value(Rig::connected_property(), true));
    }
    operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context         = context,
                .item            = bone,
                .parent          = spec.parent,
                .mode            = Item_insert_remove_operation::Mode::insert,
                .index_in_parent = std::numeric_limits<std::size_t>::max() // last child
            }
        )
    );
    operations.push_back(make_pin(bone, local));
    return bone;
}

void queue_compound(App_context& context, std::vector<std::shared_ptr<Operation>>&& operations)
{
    Compound_operation::Parameters parameters;
    parameters.operations = std::move(operations);
    context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
}

void queue_with_selection(App_context& context, std::vector<std::shared_ptr<Operation>>&& operations, const std::vector<std::shared_ptr<erhe::scene::Node>>& selected)
{
    if (context.selection != nullptr) {
        std::vector<std::shared_ptr<erhe::Item_base>> items;
        items.reserve(selected.size());
        for (const std::shared_ptr<erhe::scene::Node>& node : selected) {
            items.push_back(node);
        }
        operations.push_back(std::make_shared<Bone_selection_operation>(*context.selection, std::move(items)));
    }
    queue_compound(context, std::move(operations));
}

// The bones of `targets`, deduplicated, non-bones left out.
[[nodiscard]] auto unique_bones(const std::vector<std::shared_ptr<erhe::scene::Node>>& targets) -> std::vector<std::shared_ptr<erhe::scene::Node>>
{
    std::vector<std::shared_ptr<erhe::scene::Node>> bones;
    for (const std::shared_ptr<erhe::scene::Node>& target : targets) {
        if (!target || !erhe::scene::is_bone(target.get())) {
            continue;
        }
        if (std::find(bones.begin(), bones.end(), target) == bones.end()) {
            bones.push_back(target);
        }
    }
    return bones;
}

[[nodiscard]] auto refuse(const char* const verb, std::string reason) -> Bone_structure_result
{
    Bone_structure_result result;
    result.refusal = fmt::format("{} refused: {}", verb, reason);
    log_operations->warn("{}", result.refusal.value());
    return result;
}

// The refusal of a verb over `bones`: none named, or one bound (R9).
[[nodiscard]] auto check_targets(const char* const verb, const std::vector<std::shared_ptr<erhe::scene::Node>>& bones) -> std::optional<Bone_structure_result>
{
    if (bones.empty()) {
        return refuse(verb, "no bone to act on");
    }
    std::optional<std::string> bound = get_bound_bones_refusal(bones);
    if (bound.has_value()) {
        return refuse(verb, std::move(bound.value()));
    }
    return std::nullopt;
}

} // anonymous namespace

auto create_bone(
    App_context&                              context,
    const std::shared_ptr<erhe::scene::Node>& parent,
    const std::string_view                    name
) -> Bone_structure_result
{
    constexpr const char* verb = "Create Bone";
    if (!parent) {
        return refuse(verb, "no parent node");
    }
    const bool parent_is_bone = erhe::scene::is_bone(parent.get());
    if (parent_is_bone) {
        std::optional<std::string> bound = get_bound_bone_refusal(*parent);
        if (bound.has_value()) {
            return refuse(verb, std::move(bound.value()));
        }
    }

    New_bone spec{.parent = parent};
    if (parent_is_bone) {
        const glm::vec3 parent_tail = parent->get_value(Rig::tail_property());
        const float     length      = glm::length(parent_tail);
        spec.head = parent_tail;
        spec.tail = (length > 0.0f) ? (parent_tail / length) : glm::vec3{0.0f, 1.0f, 0.0f};
    }
    Bone_names names;
    names.add_children(*parent);
    if (parent_is_bone) {
        names.add_skeleton(parent);
    }
    spec.name = names.make(name.empty() ? std::string{"Bone"} : std::string{name});

    std::vector<std::shared_ptr<Operation>> operations;
    Bone_structure_result result;
    result.created.push_back(append_new_bone(context, spec, operations));
    queue_with_selection(context, std::move(operations), result.created);
    result.queued = true;
    log_operations->info("{}: '{}' under '{}'", verb, spec.name, parent->get_name());
    return result;
}

auto extrude_bones(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets
) -> Bone_structure_result
{
    constexpr const char* verb = "Extrude";
    const std::vector<std::shared_ptr<erhe::scene::Node>> bones = unique_bones(targets);
    std::optional<Bone_structure_result> refused = check_targets(verb, bones);
    if (refused.has_value()) {
        return std::move(refused.value());
    }

    Bone_names names;
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        names.add_skeleton(bone);
        names.add_children(*bone);
    }
    std::vector<std::shared_ptr<Operation>> operations;
    Bone_structure_result result;
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        const glm::vec3 tail = bone->get_value(Rig::tail_property());
        const New_bone spec{
            .name      = names.make_indexed(bone->get_name()),
            .parent    = bone,
            .head      = tail,
            .tail      = tail,
            .connected = true
        };
        result.created.push_back(append_new_bone(context, spec, operations));
        log_operations->info("{}: '{}' from '{}'", verb, spec.name, bone->get_name());
    }
    queue_with_selection(context, std::move(operations), result.created);
    result.queued = true;
    return result;
}

auto subdivide_bones(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets,
    const std::size_t                                      count
) -> Bone_structure_result
{
    constexpr const char* verb = "Subdivide";
    if (count < 2) {
        return refuse(verb, fmt::format("the number of pieces must be at least 2, got {}", count));
    }
    const std::vector<std::shared_ptr<erhe::scene::Node>> bones = unique_bones(targets);
    std::optional<Bone_structure_result> refused = check_targets(verb, bones);
    if (refused.has_value()) {
        return std::move(refused.value());
    }

    Bone_names names;
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        names.add_skeleton(bone);
        names.add_children(*bone);
    }
    std::vector<std::shared_ptr<Operation>>         operations;
    std::vector<std::shared_ptr<erhe::scene::Node>> selected;
    Bone_structure_result                           result;
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        const glm::vec3 tail    = bone->get_value(Rig::tail_property());
        const glm::vec3 segment = tail / static_cast<float>(count);
        // The pieces relative to the target are pure translations, so the
        // last one sits at (count - 1) * segment in the target's frame.
        const glm::vec3 last_offset = segment * static_cast<float>(count - 1);

        // The target's children, before any piece is added.
        std::vector<std::shared_ptr<erhe::Hierarchy>> children;
        for (const std::shared_ptr<erhe::Hierarchy>& child : bone->get_children()) {
            if ((child->get_flag_bits() & erhe::Item_flags::bone_proxy) != 0) {
                continue; // editor-generated display proxies stay with their bone
            }
            children.push_back(child);
        }

        selected.push_back(bone);
        std::shared_ptr<erhe::scene::Node> previous = bone;
        for (std::size_t piece = 1; piece < count; ++piece) {
            const New_bone spec{
                .name      = names.make_indexed(bone->get_name()),
                .parent    = previous,
                .head      = segment,
                .tail      = segment,
                .connected = true
            };
            previous = append_new_bone(context, spec, operations);
            result.created.push_back(previous);
            selected.push_back(previous);
        }
        const std::shared_ptr<erhe::scene::Node>& last = previous;

        // Re-parent the children to the last piece. Before: pins of the
        // current local transforms, so undo is exact; after: the local
        // transform the move implies (translation minus last_offset; a
        // connected child exactly on the last piece's tail).
        for (const std::shared_ptr<erhe::Hierarchy>& child : children) {
            const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
            if (child_node) {
                operations.push_back(make_pin(child_node, child_node->parent_from_node_transform()));
            }
            operations.push_back(std::make_shared<Item_parent_change_operation>(last, child, std::shared_ptr<erhe::Hierarchy>{}, std::shared_ptr<erhe::Hierarchy>{}));
            if (!child_node) {
                continue;
            }
            erhe::scene::Trs_transform moved = child_node->parent_from_node_transform();
            const bool connected = erhe::scene::is_bone(child_node.get()) && child_node->get_value(Rig::connected_property());
            moved.set_translation(connected ? segment : (moved.get_translation() - last_offset));
            operations.push_back(make_pin(child_node, moved));
            if (erhe::scene::is_bone(child_node.get()) && !get_bound_bone_refusal(*child_node).has_value()) {
                const glm::vec3 rest_translation = child_node->get_value(Rig::rest_translation_property());
                operations.push_back(
                    make_property_set(
                        child_node, Rig::rest_translation_property().get(),
                        child_node->read_local_state(Rig::rest_translation_property().get()),
                        Local_state{Property_value{rest_translation - last_offset}}
                    )
                );
            }
        }

        // The target's own tail shrinks to one segment. After the re-parents,
        // so its connected-children follow-up (rig/bone_connect.hpp) sees only
        // the first piece, already on the new tail.
        operations.push_back(
            make_property_set(
                bone, Rig::tail_property().get(),
                bone->read_local_state(Rig::tail_property().get()),
                Local_state{Property_value{segment}}
            )
        );
        log_operations->info("{}: '{}' into {} bones", verb, bone->get_name(), count);
    }
    queue_with_selection(context, std::move(operations), selected);
    result.queued = true;
    return result;
}

auto delete_bones(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets,
    const Bone_delete_mode                                 mode
) -> Bone_structure_result
{
    const char* const verb = get_bone_delete_mode_label(mode);
    std::vector<std::shared_ptr<erhe::scene::Node>> bones = unique_bones(targets);
    std::optional<Bone_structure_result> refused = check_targets(verb, bones);
    if (refused.has_value()) {
        return std::move(refused.value());
    }
    // Parents before children: a removed bone's children have moved to its
    // parent by the time a removed child is planned.
    std::stable_sort(
        bones.begin(), bones.end(),
        [](const std::shared_ptr<erhe::scene::Node>& lhs, const std::shared_ptr<erhe::scene::Node>& rhs) {
            return lhs->get_depth() < rhs->get_depth();
        }
    );

    // The scene as the plan leaves it, before it executes: where each moved
    // node ends up, its rest, its connected flag and each tail written.
    std::unordered_map<const erhe::scene::Node*, std::shared_ptr<erhe::Hierarchy>> planned_parent;
    std::unordered_map<const erhe::scene::Node*, erhe::scene::Trs_transform>       planned_rest;
    std::unordered_map<const erhe::scene::Node*, bool>                             planned_connected;
    std::unordered_map<const erhe::scene::Node*, std::optional<Local_state>>       planned_tail_state;
    std::unordered_set<const erhe::scene::Node*>                                   removed;

    const auto get_parent = [&planned_parent](const erhe::scene::Node& node) -> std::shared_ptr<erhe::Hierarchy> {
        const auto i = planned_parent.find(&node);
        return (i != planned_parent.end()) ? i->second : node.get_parent().lock();
    };
    const auto get_rest = [&planned_rest](const erhe::scene::Node& node) -> erhe::scene::Trs_transform {
        const auto i = planned_rest.find(&node);
        return (i != planned_rest.end()) ? i->second : read_rest_transform(node);
    };
    const auto is_connected = [&planned_connected](const erhe::scene::Node& node) -> bool {
        const auto i = planned_connected.find(&node);
        return (i != planned_connected.end()) ? i->second : node.get_value(Rig::connected_property());
    };
    const auto get_tail_state = [&planned_tail_state](const erhe::scene::Node& node) -> std::optional<Local_state> {
        const auto i = planned_tail_state.find(&node);
        return (i != planned_tail_state.end()) ? i->second : node.read_local_state(Rig::tail_property().get());
    };
    // The connected bone children `parent` has in the planned scene.
    const auto count_connected_children = [&](const erhe::Hierarchy& parent) -> std::size_t {
        std::size_t connected_count = 0;
        const auto visit = [&](const erhe::scene::Node& node) {
            if (!removed.contains(&node) && erhe::scene::is_bone(&node) && (get_parent(node).get() == &parent) && is_connected(node)) {
                ++connected_count;
            }
        };
        for (const std::shared_ptr<erhe::Hierarchy>& child : parent.get_children()) {
            const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
            if (child_node) {
                visit(*child_node);
            }
        }
        for (const auto& [node, new_parent] : planned_parent) {
            if ((new_parent.get() == &parent) && (node->get_parent().lock().get() != &parent)) {
                visit(*node);
            }
        }
        return connected_count;
    };

    std::vector<std::shared_ptr<Operation>> operations;
    Bone_structure_result                   result;
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        const std::shared_ptr<erhe::Hierarchy>   parent      = get_parent(*bone);
        const std::shared_ptr<erhe::scene::Node> parent_bone = is_bone_node(parent);

        // Dissolve: the parent's tail reaches the removed bone's tail when it
        // was the parent's only connected bone child. A bound parent's tail
        // is fixed by its bind (R9), so it is not extended.
        const bool extend_parent =
            (mode == Bone_delete_mode::dissolve) &&
            parent_bone &&
            !get_bound_bone_refusal(*parent_bone).has_value() &&
            is_connected(*bone) &&
            (count_connected_children(*parent_bone) == 1);

        std::vector<std::shared_ptr<erhe::scene::Node>> child_nodes;
        for (const std::shared_ptr<erhe::Hierarchy>& child : bone->get_children()) {
            if ((child->get_flag_bits() & erhe::Item_flags::bone_proxy) != 0) {
                continue; // removed with the bone, as Item_insert_remove_operation does
            }
            const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
            if (child_node) {
                child_nodes.push_back(child_node);
                operations.push_back(make_pin(child_node, child_node->parent_from_node_transform()));
            }
        }

        // Removal: Item_insert_remove_operation re-parents the children to
        // the bone's parent (world transforms kept) before detaching it.
        operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context = context,
                    .item    = bone,
                    .parent  = parent,
                    .mode    = Item_insert_remove_operation::Mode::remove
                }
            )
        );
        removed.insert(bone.get());
        result.removed.push_back(bone);

        const erhe::scene::Trs_transform bone_rest = get_rest(*bone);
        for (const std::shared_ptr<erhe::scene::Node>& child : child_nodes) {
            planned_parent[child.get()] = parent;
            if (!erhe::scene::is_bone(child.get()) || get_bound_bone_refusal(*child).has_value()) {
                continue;
            }
            // The rest stays where it was: rest(removed) * rest(child).
            const erhe::scene::Trs_transform child_rest = erhe::scene::Trs_transform{bone_rest.get_matrix() * get_rest(*child).get_matrix()};
            planned_rest[child.get()] = child_rest;
            operations.push_back(make_property_set(child, Rig::rest_translation_property().get(), child->read_local_state(Rig::rest_translation_property().get()), Local_state{Property_value{child_rest.get_translation()}}));
            operations.push_back(make_property_set(child, Rig::rest_rotation_property   ().get(), child->read_local_state(Rig::rest_rotation_property   ().get()), Local_state{Property_value{child_rest.get_rotation()}}));
            operations.push_back(make_property_set(child, Rig::rest_scale_property      ().get(), child->read_local_state(Rig::rest_scale_property      ().get()), Local_state{Property_value{child_rest.get_scale()}}));
            if (is_connected(*child) && !extend_parent) {
                operations.push_back(make_property_set(child, Rig::connected_property().get(), child->read_local_state(Rig::connected_property().get()), std::nullopt));
                planned_connected[child.get()] = false;
            }
        }

        if (extend_parent) {
            // The removed bone's tail in the parent's frame; world transforms
            // are what the re-parents keep, so they are the plan's too.
            const glm::vec3 bone_tail = bone->get_value(Rig::tail_property());
            const glm::vec4 tail_in_world  = bone->world_from_node() * glm::vec4{bone_tail, 1.0f};
            const glm::vec4 tail_in_parent = glm::inverse(parent_bone->world_from_node()) * tail_in_world;
            const glm::vec3 new_tail{tail_in_parent};
            // After the removal, so the tail edit's connected-children
            // follow-up snaps the moved connected children exactly onto it.
            operations.push_back(make_property_set(parent_bone, Rig::tail_property().get(), get_tail_state(*parent_bone), Local_state{Property_value{new_tail}}));
            planned_tail_state[parent_bone.get()] = Local_state{Property_value{new_tail}};
            log_operations->info("{}: '{}' removed, tail of '{}' extended to its tail", verb, bone->get_name(), parent_bone->get_name());
        } else {
            log_operations->info("{}: '{}' removed, {} child node(s) re-parented", verb, bone->get_name(), child_nodes.size());
        }
    }
    queue_compound(context, std::move(operations));
    result.queued = true;
    return result;
}

namespace {

// Rs(node) of rig/bone_mirror.hpp: the rest transform of `node` relative to
// the skeleton frame - the product of the Rig.rest_* local transforms from
// `root` down to `node`; identity for the skeleton frame's own node (the
// root's parent, not a bone).
[[nodiscard]] auto get_rest_in_skeleton_frame(const std::shared_ptr<erhe::scene::Node>& node, const std::shared_ptr<erhe::scene::Node>& root) -> glm::mat4
{
    glm::mat4 rest{1.0f};
    for (std::shared_ptr<erhe::scene::Node> current = node; current && erhe::scene::is_bone(current.get()); current = current->get_parent_node()) {
        rest = read_rest_transform(*current).get_matrix() * rest;
        if (current == root) {
            break;
        }
    }
    return rest;
}

[[nodiscard]] auto find_child_named(const erhe::Hierarchy& parent, const std::string& name) -> std::shared_ptr<erhe::scene::Node>
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : parent.get_children()) {
        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (node && (node->get_name() == name)) {
            return node;
        }
    }
    return {};
}

using Mirror_map = std::unordered_map<const erhe::scene::Node*, std::shared_ptr<erhe::scene::Node>>;

// The pole target of a mirror bone: the mirror of the source's pole - the
// mirror bone Symmetrize creates for it, the pole bone's counterpart, or the
// pole's sibling of the flipped name. Null when there is none.
[[nodiscard]] auto find_mirror_pole(const std::shared_ptr<erhe::scene::Node>& pole, const Mirror_map& mirror_of) -> std::shared_ptr<erhe::scene::Node>
{
    if (!pole) {
        return {};
    }
    const auto created = mirror_of.find(pole.get());
    if (created != mirror_of.end()) {
        return created->second;
    }
    if (bone_side(pole->get_name()) == Bone_side::none) {
        return {};
    }
    if (erhe::scene::is_bone(pole.get())) {
        std::shared_ptr<erhe::scene::Node> counterpart = find_mirror_bone(pole);
        if (counterpart) {
            return counterpart;
        }
    }
    const std::shared_ptr<erhe::scene::Node> parent = pole->get_parent_node();
    return parent ? find_child_named(*parent, flip_side_name(pole->get_name())) : std::shared_ptr<erhe::scene::Node>{};
}

// How a mirror bone's parent frame relates to its source's.
enum class Mirror_parent : unsigned int {
    // The new parent is the mirror of the source's parent, or the source is a
    // skeleton root (its parent frame is the skeleton frame): the parent map
    // is S itself, and the TRS forms of rig/bone_mirror.hpp are exact.
    mirrored = 0,
    // Any other new parent: the general parent map.
    general  = 1
};

// Copies the local Ik.* values of `source` onto its new mirror bone `target`
// (an orphan node before its insert, so plain writes: the insert carries
// them), mirrored as rig/bone_mirror.hpp says.
void copy_mirrored_ik_values(const erhe::scene::Node& source, erhe::scene::Node& target, const glm::mat4& rest_parent_map, const Mirror_parent mirror_parent)
{
    for (int axis = 0; axis < 3; ++axis) {
        if (source.has_local_value(Ik::lock_property(axis).get())) {
            target.set_value(Ik::lock_property(axis), source.get_value(Ik::lock_property(axis)));
        }
        if (source.has_local_value(Ik::limit_property(axis).get())) {
            target.set_value(Ik::limit_property(axis), source.get_value(Ik::limit_property(axis)));
        }
    }
    if (source.has_local_value(Ik::limit_min_property.get()) || source.has_local_value(Ik::limit_max_property.get())) {
        const Ik_limit_range range = mirror_ik_limits(source.get_value(Ik::limit_min_property), source.get_value(Ik::limit_max_property));
        target.set_value(Ik::limit_min_property, range.min);
        target.set_value(Ik::limit_max_property, range.max);
    }
    if (source.has_local_value(Ik::stiffness_property.get())) {
        target.set_value(Ik::stiffness_property, source.get_value(Ik::stiffness_property));
    }
    if (source.has_local_value(Ik::rest_rotation_property.get())) {
        const glm::quat rest_rotation = source.get_value(Ik::rest_rotation_property);
        const glm::quat mirrored = (mirror_parent == Mirror_parent::mirrored)
            ? mirror_rotation_x(rest_rotation)
            : glm::normalize(mirror_local_transform(rest_parent_map, glm::mat4_cast(rest_rotation)).get_rotation());
        target.set_value(Ik::rest_rotation_property, mirrored);
    }
    if (source.has_local_value(Ik::pole_angle_property.get())) {
        target.set_value(Ik::pole_angle_property, mirror_pole_angle(source.get_value(Ik::pole_angle_property)));
    }
}

} // anonymous namespace

auto symmetrize_bones(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets
) -> Bone_structure_result
{
    constexpr const char* verb = "Symmetrize";
    std::vector<std::shared_ptr<erhe::scene::Node>> bones = unique_bones(targets);
    std::optional<Bone_structure_result> refused = check_targets(verb, bones);
    if (refused.has_value()) {
        return std::move(refused.value());
    }
    // Parents before children: a target's mirror parent exists (planned) by
    // the time the target is mirrored.
    std::stable_sort(
        bones.begin(), bones.end(),
        [](const std::shared_ptr<erhe::scene::Node>& lhs, const std::shared_ptr<erhe::scene::Node>& rhs) {
            return lhs->get_depth() < rhs->get_depth();
        }
    );

    Bone_names names;
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        names.add_skeleton(bone);
    }

    class Pole_link
    {
    public:
        std::shared_ptr<erhe::scene::Node> source;
        std::shared_ptr<erhe::scene::Node> mirror;
    };

    Mirror_map                                              mirror_of;    // target -> its new mirror bone
    std::unordered_map<const erhe::scene::Node*, glm::vec3> planned_tail; // new mirror bone -> its tail
    std::vector<Pole_link>                                  pole_links;
    std::vector<std::shared_ptr<Operation>>                 operations;
    Bone_structure_result                                   result;
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        const std::string& name = bone->get_name();
        if (bone_side(name) == Bone_side::none) {
            log_operations->info("{}: '{}' has no side in its name, skipped", verb, name);
            continue;
        }
        const std::shared_ptr<erhe::scene::Node> root    = get_skeleton_root(bone);
        const std::shared_ptr<erhe::scene::Node> parent  = bone->get_parent_node();
        const bool                               is_root = (root == bone);
        const std::string                        flipped = flip_side_name(name);
        if (!parent) {
            log_operations->info("{}: '{}' has no parent node, skipped", verb, name);
            continue;
        }
        // A skeleton root's counterpart is a separate skeleton (R12): its
        // sibling of the flipped name.
        const std::shared_ptr<erhe::scene::Node> counterpart = is_root ? find_child_named(*parent, flipped) : find_mirror_bone(bone);
        if (counterpart) {
            log_operations->info("{}: '{}' already has its counterpart '{}', skipped", verb, name, counterpart->get_name());
            continue;
        }

        std::shared_ptr<erhe::scene::Node> new_parent;
        Mirror_parent                      mirror_parent = Mirror_parent::mirrored;
        const auto created_parent = mirror_of.find(parent.get());
        if (created_parent != mirror_of.end()) {
            new_parent = created_parent->second;
        } else if (is_root) {
            new_parent = parent;
        } else {
            const bool parent_has_side = (bone_side(parent->get_name()) != Bone_side::none);
            const std::shared_ptr<erhe::scene::Node> parent_counterpart = parent_has_side ? find_mirror_bone(parent) : std::shared_ptr<erhe::scene::Node>{};
            if (parent_has_side && !parent_counterpart) {
                log_operations->info("{}: parent '{}' of '{}' has no counterpart, the mirror bone goes under it", verb, parent->get_name(), name);
            }
            new_parent    = parent_counterpart ? parent_counterpart : parent;
            mirror_parent = Mirror_parent::general;
            // R9, as for Create Bone: no new bone under a bone a skin lists.
            std::optional<std::string> bound = get_bound_bone_refusal(*new_parent);
            if (bound.has_value()) {
                return refuse(verb, std::move(bound.value()));
            }
        }

        // The mirror in the skeleton frame, of the current pose and of the
        // rest (rig/bone_mirror.hpp).
        const erhe::scene::Trs_transform& local = bone->parent_from_node_transform();
        const erhe::scene::Trs_transform  rest  = read_rest_transform(*bone);
        erhe::scene::Trs_transform        new_local;
        erhe::scene::Trs_transform        new_rest;
        glm::mat4                         rest_parent_map = get_mirror_x_matrix();
        if (mirror_parent == Mirror_parent::mirrored) {
            new_local = mirror_trs_x(local);
            new_rest  = mirror_trs_x(rest);
        } else {
            const std::shared_ptr<erhe::scene::Node> frame_node = root->get_parent_node();
            const glm::mat4 frame           = frame_node ? frame_node->world_from_node() : glm::mat4{1.0f};
            const glm::mat4 pose_parent_map = glm::inverse(new_parent->world_from_node()) * get_mirror_plane_matrix(frame) * parent->world_from_node();
            rest_parent_map = glm::inverse(get_rest_in_skeleton_frame(new_parent, root)) * get_mirror_x_matrix() * get_rest_in_skeleton_frame(parent, root);
            new_local = mirror_local_transform(pose_parent_map, local.get_matrix());
            new_rest  = mirror_local_transform(rest_parent_map, rest.get_matrix());
        }

        const glm::vec3 tail      = mirror_vector_x(bone->get_value(Rig::tail_property()));
        bool            connected = bone->get_value(Rig::connected_property());
        if (connected && erhe::scene::is_bone(new_parent.get())) {
            const auto      planned     = planned_tail.find(new_parent.get());
            const glm::vec3 parent_tail = (planned != planned_tail.end()) ? planned->second : new_parent->get_value(Rig::tail_property());
            const float     tolerance   = 1.0e-4f * std::max(1.0f, glm::length(parent_tail));
            if (glm::length(new_local.get_translation() - parent_tail) > tolerance) {
                connected = false;
                log_operations->info("{}: the mirror of '{}' is not on the tail of '{}', so it is not connected", verb, name, new_parent->get_name());
            }
        }

        if (created_parent == mirror_of.end()) {
            names.add_children(*new_parent);
        }
        const New_bone spec{
            .name      = names.make(flipped),
            .parent    = new_parent,
            .head      = new_local.get_translation(),
            .tail      = tail,
            .connected = connected,
            .rotation  = new_local.get_rotation(),
            .scale     = new_local.get_scale(),
            .rest      = new_rest
        };
        const std::shared_ptr<erhe::scene::Node> mirror = append_new_bone(context, spec, operations);
        copy_mirrored_ik_values(*bone, *mirror, rest_parent_map, mirror_parent);
        if (bone->has_local_value(Ik::pole_target_property.get())) {
            pole_links.push_back(Pole_link{.source = bone, .mirror = mirror});
        }
        mirror_of[bone.get()]      = mirror;
        planned_tail[mirror.get()] = tail;
        result.created.push_back(mirror);
        log_operations->info("{}: '{}' mirrored as '{}' under '{}'", verb, name, spec.name, new_parent->get_name());
    }

    // Poles last, so a pole mirrored in this same step is found.
    for (const Pole_link& link : pole_links) {
        const std::shared_ptr<erhe::scene::Node> pole     = get_ik_pole_target(*link.source);
        const std::shared_ptr<erhe::scene::Node> mirrored = find_mirror_pole(pole, mirror_of);
        if (mirrored) {
            set_ik_pole_target(*link.mirror, mirrored);
        } else if (pole) {
            log_operations->info("{}: pole target '{}' of '{}' has no mirror, not copied", verb, pole->get_name(), link.source->get_name());
        }
    }

    if (result.created.empty()) {
        log_operations->info("{}: nothing to mirror", verb);
        return result;
    }
    queue_with_selection(context, std::move(operations), result.created);
    result.queued = true;
    return result;
}

auto get_roll_axis_label(const Roll_axis axis) -> const char*
{
    switch (axis) {
        case Roll_axis::x: return "X";
        case Roll_axis::z: return "Z";
        default:           return "?";
    }
}

namespace {

class Bone_frame_change
{
public:
    std::shared_ptr<erhe::scene::Node> bone;
    glm::quat                          change{1.0f, 0.0f, 0.0f, 0.0f};
};

// Whether a frame-changing verb records a target's default Rig.tail as a
// local value.
enum class Tail_record : unsigned int {
    keep         = 0,
    record_local = 1
};

// The compound of Recalculate Roll / Align to Active (bone_structure.hpp).
[[nodiscard]] auto apply_bone_frame_changes(App_context& context, const std::vector<Bone_frame_change>& changes, const Tail_record tail_record) -> Bone_structure_result
{
    const glm::quat identity{1.0f, 0.0f, 0.0f, 0.0f};
    std::unordered_map<const erhe::scene::Node*, glm::quat> own_changes;
    for (const Bone_frame_change& change : changes) {
        own_changes[change.bone.get()] = change.change;
    }
    const auto own_change_of = [&own_changes, &identity](const erhe::scene::Node* node) -> glm::quat {
        const auto i = own_changes.find(node);
        return (i != own_changes.end()) ? i->second : identity;
    };

    // The targets and their children: the nodes whose local transform
    // changes.
    std::vector<std::shared_ptr<erhe::scene::Node>> affected;
    const auto add = [&affected](const std::shared_ptr<erhe::scene::Node>& node) {
        if (std::find(affected.begin(), affected.end(), node) == affected.end()) {
            affected.push_back(node);
        }
    };
    for (const Bone_frame_change& change : changes) {
        add(change.bone);
        for (const std::shared_ptr<erhe::Hierarchy>& child : change.bone->get_children()) {
            if ((child->get_flag_bits() & erhe::Item_flags::bone_proxy) != 0) {
                continue; // editor-generated display proxies follow their bone
            }
            const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
            if (child_node) {
                add(child_node);
            }
        }
    }

    std::vector<std::shared_ptr<Operation>> operations;
    std::vector<std::shared_ptr<Operation>> tail_operations;
    Bone_structure_result                   result;
    for (const std::shared_ptr<erhe::scene::Node>& node : affected) {
        const std::shared_ptr<erhe::scene::Node> parent = node->get_parent_node();
        const bool              parent_changes = parent && own_changes.contains(parent.get());
        const glm::quat         parent_change  = parent_changes ? own_change_of(parent.get()) : identity;
        const glm::quat         own_change     = own_change_of(node.get());
        const bool              is_bone        = erhe::scene::is_bone(node.get());
        const Frame_change_head head           = (is_bone && parent_changes && node->get_value(Rig::connected_property()))
            ? Frame_change_head::on_parent_tail
            : Frame_change_head::keep_world;

        const erhe::scene::Trs_transform& before = node->parent_from_node_transform();
        operations.push_back(
            std::make_shared<Node_transform_operation>(
                Node_transform_operation::Parameters{
                    .node                    = node,
                    .parent_from_node_before = before,
                    .parent_from_node_after  = apply_frame_change(before, parent_change, own_change, head),
                    .xform_op_stack_before   = node->copy_xform_op_stack(),
                    .time_duration           = 0.0f
                }
            )
        );
        // The rest turns with the frame, so the pose relative to rest is
        // unchanged. A bound bone's rest is its bind (R9): only a bound
        // child of a target gets here, and its rest is left alone.
        if (is_bone && !get_bound_bone_refusal(*node).has_value()) {
            const erhe::scene::Trs_transform new_rest = apply_frame_change(read_rest_transform(*node), parent_change, own_change, head);
            operations.push_back(make_property_set(node, Rig::rest_translation_property().get(), node->read_local_state(Rig::rest_translation_property().get()), Local_state{Property_value{new_rest.get_translation()}}));
            operations.push_back(make_property_set(node, Rig::rest_rotation_property   ().get(), node->read_local_state(Rig::rest_rotation_property   ().get()), Local_state{Property_value{new_rest.get_rotation()}}));
        }
        // A local IK limits frame turns with it too; a default one follows
        // Rig.rest_rotation.
        if (node->has_local_value(Ik::rest_rotation_property.get())) {
            const glm::quat ik_rest     = node->get_value(Ik::rest_rotation_property);
            const glm::quat new_ik_rest = glm::normalize(glm::inverse(parent_change) * ik_rest * own_change);
            operations.push_back(make_property_set(node, Ik::rest_rotation_property.get(), node->read_local_state(Ik::rest_rotation_property.get()), Local_state{Property_value{new_ik_rest}}));
        }
        if (own_changes.contains(node.get())) {
            result.changed.push_back(node);
            if ((tail_record == Tail_record::record_local) && !node->has_local_value(Rig::tail_property().get())) {
                tail_operations.push_back(make_property_set(node, Rig::tail_property().get(), std::nullopt, Local_state{Property_value{node->get_value(Rig::tail_property())}}));
            }
        }
    }
    // Tail records last: their connected-children follow-ups
    // (rig/bone_connect.hpp) then find the children already in place.
    for (std::shared_ptr<Operation>& operation : tail_operations) {
        operations.push_back(std::move(operation));
    }
    queue_compound(context, std::move(operations));
    result.queued = true;
    return result;
}

} // anonymous namespace

auto recalculate_bone_roll(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets,
    const Roll_axis                                        axis,
    const glm::vec3&                                       reference
) -> Bone_structure_result
{
    constexpr const char* verb = "Recalculate Roll";
    const std::vector<std::shared_ptr<erhe::scene::Node>> bones = unique_bones(targets);
    std::optional<Bone_structure_result> refused = check_targets(verb, bones);
    if (refused.has_value()) {
        return std::move(refused.value());
    }
    const glm::vec3 local_axis = get_roll_axis_vector(axis);
    std::vector<Bone_frame_change> changes;
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        const glm::quat                world_rotation = bone->world_from_node_transform().get_rotation();
        const glm::vec3                tail           = bone->get_value(Rig::tail_property());
        const std::optional<float>     angle          = compute_roll_angle(world_rotation, tail, local_axis, reference);
        const std::optional<glm::quat> change         = angle.has_value() ? make_roll_change(tail, angle.value()) : std::optional<glm::quat>{};
        if (!change.has_value()) {
            log_operations->info("{}: '{}' skipped: its {} axis or the reference is parallel to its bone axis", verb, bone->get_name(), get_roll_axis_label(axis));
            continue;
        }
        changes.push_back(Bone_frame_change{.bone = bone, .change = change.value()});
        log_operations->info("{}: '{}' rolled by {:.4f} degrees", verb, bone->get_name(), glm::degrees(angle.value()));
    }
    if (changes.empty()) {
        return refuse(verb, fmt::format("no target bone can aim its {} axis at the reference (parallel to the bone axis)", get_roll_axis_label(axis)));
    }
    return apply_bone_frame_changes(context, changes, Tail_record::keep);
}

auto align_bones_to_active(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets,
    const std::shared_ptr<erhe::scene::Node>&              active
) -> Bone_structure_result
{
    constexpr const char* verb = "Align to Active";
    if (!active || !erhe::scene::is_bone(active.get())) {
        return refuse(verb, "the active item is not a bone");
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> bones = unique_bones(targets);
    bones.erase(std::remove(bones.begin(), bones.end(), active), bones.end());
    std::optional<Bone_structure_result> refused = check_targets(verb, bones);
    if (refused.has_value()) {
        return std::move(refused.value());
    }
    const glm::quat active_rotation = active->world_from_node_transform().get_rotation();
    const glm::vec3 active_tail     = active->get_value(Rig::tail_property());
    std::vector<Bone_frame_change> changes;
    for (const std::shared_ptr<erhe::scene::Node>& bone : bones) {
        const std::optional<glm::quat> change = compute_align_change(
            bone->world_from_node_transform().get_rotation(), bone->get_value(Rig::tail_property()), active_rotation, active_tail
        );
        if (!change.has_value()) {
            log_operations->info("{}: '{}' skipped: it or '{}' has a zero tail", verb, bone->get_name(), active->get_name());
            continue;
        }
        changes.push_back(Bone_frame_change{.bone = bone, .change = change.value()});
        log_operations->info("{}: '{}' aligned to '{}'", verb, bone->get_name(), active->get_name());
    }
    if (changes.empty()) {
        return refuse(verb, fmt::format("no target bone can be aligned to '{}' (zero tail)", active->get_name()));
    }
    return apply_bone_frame_changes(context, changes, Tail_record::record_local);
}

}
