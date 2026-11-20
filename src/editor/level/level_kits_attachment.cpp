// Step 41 of doc/plans/level_tools_implementation.md: persistent connector attachments (doc/editor/level_tools.md "Attachments").

#include "level/level_kits.hpp"
#include "level/kit_layout.hpp"
#include "level/kit_piece.hpp"
#include "level/kit_piece_operation.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "operations/compound_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"
#include "scene/scene_root.hpp"

#include "erhe_item/item_host.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace editor {

namespace {

// The nodes of a scene carrying one uid: the first of them and how many there are.
class Uid_lookup
{
public:
    std::shared_ptr<erhe::scene::Node> node;
    std::size_t                        count{0};
};

[[nodiscard]] auto find_uid(const Scene_root& scene_root, const std::string_view uid) -> Uid_lookup
{
    Uid_lookup lookup;
    scene_root.get_scene().for_each_node(
        [&lookup, uid](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
            if (node->get_gltf_uid() == uid) {
                if (lookup.count == 0) {
                    lookup.node = node;
                }
                ++lookup.count;
            }
            return true;
        }
    );
    return lookup;
}

constexpr const char* c_target_missing   = "the target is not in the scene";
constexpr const char* c_target_ambiguous = "ambiguous target: its uid names more than one node of the scene";
constexpr const char* c_locked_layout    = "a child of a locked kit layout node takes no part in an attachment";

[[nodiscard]] auto contains(const std::vector<const erhe::scene::Node*>& nodes, const erhe::scene::Node* node) -> bool
{
    return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
}

// The node or one of its ancestors is in the list.
[[nodiscard]] auto is_at_or_below(const std::vector<const erhe::scene::Node*>& nodes, const erhe::scene::Node* node) -> bool
{
    for (const erhe::scene::Node* i = node; i != nullptr; i = i->get_parent_node().get()) {
        if (contains(nodes, i)) {
            return true;
        }
    }
    return false;
}

// The rigid motion putting the attached piece on its join, from the current world transforms; nullopt when a frame degenerates.
[[nodiscard]] auto attachment_motion(
    const erhe::scene::Node& node,
    const Kit_connector&     own,
    const erhe::scene::Node& target,
    const Kit_connector&     target_local,
    const int                twist_steps,
    const float              gap
) -> std::optional<Kit_join_pose>
{
    const std::optional<Kit_connector> target_world = transform_connector(target_local, target.world_from_node());
    if (!target_world.has_value()) {
        return std::nullopt;
    }
    return join_motion(own, node.world_from_node(), target_world.value(), twist_steps, gap);
}

[[nodiscard]] auto is_on_join(const erhe::scene::Node& node, const Kit_connector& own, const erhe::scene::Node& target, const Kit_connector& target_local, const Kit_attachment& attachment) -> bool
{
    const std::optional<Kit_join_pose> motion = attachment_motion(node, own, target, target_local, attachment.twist_steps, attachment.gap);
    return motion.has_value() && is_identity_motion(motion.value());
}

} // anonymous namespace

auto Level_kits::make_connect_attachment(const erhe::scene::Node& node, const Kit_connect& connect) -> Kit_attachment
{
    return Kit_attachment{
        .owner            = node.get_gltf_uid(),
        .target           = connect.target ? connect.target->get_gltf_uid() : std::string{},
        .connector        = connect.connector,
        .target_connector = connect.target_connector,
        .twist_steps      = ((connect.twist_steps % 4) + 4) % 4, // the same join
        .gap              = connect.gap
    };
}

auto Level_kits::make_attachment_operation(const std::shared_ptr<erhe::scene::Node>& node, const std::optional<Kit_attachment>& attachment) -> std::shared_ptr<Operation>
{
    const erhe::property::Dependency_property& property = Kit_piece::attachment_property.get();
    const std::optional<erhe::property::Local_state> before = node->read_local_state(property);
    const std::optional<std::string> text_before = node->read_local_value(Kit_piece::attachment_property);
    const std::string text = attachment.has_value() ? make_attachment_text(attachment.value()) : std::string{};
    if (text == text_before.value_or(std::string{})) {
        return {};
    }
    // No attachment is no local value, so a piece that follows nothing carries none in the file.
    return std::make_shared<Property_set_operation>(
        node, property, before,
        text.empty() ? std::optional<erhe::property::Local_state>{} : std::optional<erhe::property::Local_state>{erhe::property::Property_value{text}}
    );
}

auto Level_kits::check_attach(const erhe::scene::Node& node, const erhe::scene::Node& target) const -> Kit_attach_check
{
    if (find_locking_kit_layout(node)) {
        return Kit_attach_check{Kit_attach_refusal::locked_layout, fmt::format("'{}' is a child of a locked kit layout node, which owns its pose", node.get_name())};
    }
    if (find_locking_kit_layout(target)) {
        return Kit_attach_check{
            Kit_attach_refusal::locked_layout, fmt::format("target '{}' is a child of a locked kit layout node, which regenerates its children", target.get_name())
        };
    }
    const Scene_root* const scene_root = dynamic_cast<const Scene_root*>(target.get_item_host());
    if (scene_root == nullptr) {
        return {};
    }
    // A chain of attachments from the target must not lead back to the node.
    const erhe::scene::Node* current = &target;
    std::vector<const erhe::scene::Node*> visited;
    while (current != nullptr) {
        if (current == &node) {
            return Kit_attach_check{Kit_attach_refusal::cycle, fmt::format("attaching '{}' to '{}' would close a cycle of attachments", node.get_name(), target.get_name())};
        }
        if (contains(visited, current)) {
            break;
        }
        visited.push_back(current);
        const std::optional<Kit_attachment> attachment = read_kit_attachment(*current);
        if (!attachment.has_value()) {
            break;
        }
        const Uid_lookup next = find_uid(*scene_root, attachment.value().target);
        current = next.node.get();
    }
    return {};
}

auto Level_kits::get_attachment_status(const erhe::scene::Node& node) const -> Kit_attachment_status
{
    Kit_attachment_status status;
    const std::optional<Kit_attachment> attachment = read_kit_attachment(node);
    if (!attachment.has_value()) {
        return status;
    }
    status.attachment = attachment.value();
    status.state      = Kit_attachment_state::dormant;
    const Scene_root* const scene_root = dynamic_cast<const Scene_root*>(node.get_item_host());
    if (scene_root == nullptr) {
        status.reason = "the piece is not in a scene";
        return status;
    }
    if (find_uid(*scene_root, node.get_gltf_uid()).count > 1) {
        status.reason = "ambiguous owner: the piece's uid names more than one node of the scene (the copies are inert)";
        return status;
    }
    const Uid_lookup target = find_uid(*scene_root, status.attachment.target);
    if (target.count > 1) {
        status.reason = c_target_ambiguous;
        return status;
    }
    status.target = target.node;
    if (!status.target) {
        status.reason = c_target_missing;
        return status;
    }
    if (find_locking_kit_layout(node) || find_locking_kit_layout(*status.target)) {
        status.reason = c_locked_layout;
        return status;
    }
    const std::array<std::pair<const erhe::scene::Node*, const std::string*>, 2> ends{
        std::pair<const erhe::scene::Node*, const std::string*>{&node,                &status.attachment.connector},
        std::pair<const erhe::scene::Node*, const std::string*>{status.target.get(), &status.attachment.target_connector}
    };
    for (const std::pair<const erhe::scene::Node*, const std::string*>& end : ends) {
        std::vector<Kit_connector> connectors;
        const std::string problem = get_local_connectors(*end.first, connectors, Kit_connector_set::all);
        if (!problem.empty()) {
            status.reason = problem;
            return status;
        }
        const Kit_connector* const connector = find_connector(connectors, *end.second);
        if (connector == nullptr) {
            status.reason = fmt::format("'{}' has no connector '{}'", end.first->get_name(), *end.second);
            return status;
        }
        const std::size_t index = static_cast<std::size_t>(connector - connectors.data());
        if (!get_connector_switches(*end.first, connectors).is_enabled(*end.second, index)) {
            status.reason = fmt::format("connector '{}' of '{}' is disabled", *end.second, end.first->get_name());
            return status;
        }
    }
    status.state = Kit_attachment_state::active;
    return status;
}

auto Level_kits::detach_piece(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node) -> Kit_detach_result
{
    Kit_detach_result result;
    if (!node || (node->get_item_host() != &scene_root)) {
        result.error = node ? fmt::format("node '{}' is not in scene '{}'", node->get_name(), scene_root.get_name()) : std::string{"no node"};
        return result;
    }
    if (!carries_kit_piece(*node)) {
        result.error = fmt::format("'{}' is not a kit piece", node->get_name());
        return result;
    }
    // A record copied from another node (not its own) is removed too.
    const std::shared_ptr<Operation> operation = make_attachment_operation(node, std::nullopt);
    if (!operation) {
        return result;
    }
    m_context.operation_stack->execute_now(operation);
    log_scene->info("Kit piece '{}' detached", node->get_name());
    result.changed = true;
    return result;
}

auto Level_kits::find_snap_attachment(const Kit_snap_pair& snap) const -> std::optional<Kit_attachment>
{
    if (!snap.piece || !snap.target || (snap.piece->get_item_host() == nullptr) || (snap.piece->get_item_host() != snap.target->get_item_host())) {
        return std::nullopt;
    }
    if (check_attach(*snap.piece, *snap.target).refusal != Kit_attach_refusal::none) {
        return std::nullopt;
    }
    const Scene_root* const scene_root = static_cast<const Scene_root*>(snap.piece->get_item_host());
    if ((find_uid(*scene_root, snap.piece->get_gltf_uid()).count != 1) || (find_uid(*scene_root, snap.target->get_gltf_uid()).count != 1)) {
        return std::nullopt;
    }
    std::vector<Kit_connector> connectors;
    std::vector<Kit_connector> target_connectors;
    if (!get_local_connectors(*snap.piece, connectors, Kit_connector_set::enabled).empty() ||
        !get_local_connectors(*snap.target, target_connectors, Kit_connector_set::enabled).empty()) {
        return std::nullopt;
    }
    const Kit_connector* const own          = find_connector(connectors, snap.connector);
    const Kit_connector* const target_local = find_connector(target_connectors, snap.target_connector);
    if ((own == nullptr) || (target_local == nullptr)) {
        return std::nullopt;
    }
    // Only where the poses being recorded are a join of the pair: a translation-only snap leaves the frames unaligned.
    for (int twist = 0; twist < 4; ++twist) {
        const std::optional<Kit_join_pose> motion = attachment_motion(*snap.piece, *own, *snap.target, *target_local, twist, 0.0f);
        if (motion.has_value() && is_identity_motion(motion.value())) {
            return Kit_attachment{
                .owner            = snap.piece->get_gltf_uid(),
                .target           = snap.target->get_gltf_uid(),
                .connector        = snap.connector,
                .target_connector = snap.target_connector,
                .twist_steps      = twist,
                .gap              = 0.0f
            };
        }
    }
    return std::nullopt;
}

auto Level_kits::has_attachments(const Scene_root& scene_root) -> bool
{
    bool found = false;
    scene_root.get_scene().for_each_node(
        [&found](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
            found = node->has_local_value(Kit_piece::attachment_property.get());
            return !found;
        }
    );
    return found;
}

auto Level_kits::make_attachment_follow_operation(
    Scene_root&                                               scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
    const std::span<const std::shared_ptr<erhe::scene::Node>> changed_pieces,
    const Kit_snap_pair* const                                snap
) -> std::shared_ptr<Operation>
{
    const bool snapped = (snap != nullptr) && snap->piece && snap->target;
    if (!snapped && !has_attachments(scene_root)) {
        return {};
    }
    return std::make_shared<Kit_attachment_follow_operation>(
        std::vector<std::shared_ptr<erhe::scene::Node>>{moved_nodes.begin(), moved_nodes.end()},
        std::vector<std::shared_ptr<erhe::scene::Node>>{changed_pieces.begin(), changed_pieces.end()},
        snapped ? std::optional<Kit_snap_pair>{*snap} : std::optional<Kit_snap_pair>{}
    );
}

void Level_kits::append_attachment_follow_ups(
    const std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
    std::vector<std::shared_ptr<Operation>>&                  out_operations
)
{
    std::vector<Scene_root*>                        scene_roots;
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    for (const std::shared_ptr<erhe::scene::Node>& node : moved_nodes) {
        Scene_root* const scene_root = node ? dynamic_cast<Scene_root*>(node->get_item_host()) : nullptr;
        if ((scene_root != nullptr) && (std::find(scene_roots.begin(), scene_roots.end(), scene_root) == scene_roots.end())) {
            scene_roots.push_back(scene_root);
        }
    }
    for (Scene_root* const scene_root : scene_roots) {
        nodes.clear();
        for (const std::shared_ptr<erhe::scene::Node>& node : moved_nodes) {
            if (node && (node->get_item_host() == scene_root)) {
                nodes.push_back(node);
            }
        }
        std::shared_ptr<Operation> follow = make_attachment_follow_operation(*scene_root, nodes, {});
        if (follow) {
            out_operations.push_back(std::move(follow));
        }
    }
}

void Level_kits::append_kit_piece_record_follow_ups(
    const std::span<const std::shared_ptr<erhe::Item_base>> items,
    const erhe::property::Dependency_property&              property,
    std::vector<std::shared_ptr<Operation>>&                out_operations
)
{
    if ((&property != &Kit_piece::parameters_property.get()) && (&property != &Kit_piece::shape_property.get())) {
        return;
    }
    std::vector<Scene_root*>                        scene_roots;
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        Scene_root* const scene_root = node ? dynamic_cast<Scene_root*>(node->get_item_host()) : nullptr;
        if ((scene_root != nullptr) && (std::find(scene_roots.begin(), scene_roots.end(), scene_root) == scene_roots.end())) {
            scene_roots.push_back(scene_root);
        }
    }
    for (Scene_root* const scene_root : scene_roots) {
        nodes.clear();
        for (const std::shared_ptr<erhe::Item_base>& item : items) {
            std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if (node && (node->get_item_host() == scene_root)) {
                nodes.push_back(std::move(node));
            }
        }
        std::shared_ptr<Operation> follow = make_attachment_follow_operation(*scene_root, {}, nodes);
        if (follow) {
            out_operations.push_back(std::move(follow));
        }
    }
}

auto Level_kits::resolve_connector_of_values(
    const Kit_parameter_values&   values,
    const Kit_connector_switches& switches,
    const std::string&            name,
    Kit_connector&                out_connector
) -> const char*
{
    get_shape_connectors(values, m_follow_connectors);
    const Kit_connector* const connector = find_connector(m_follow_connectors, name);
    if (connector == nullptr) {
        return "a connector the shape does not have";
    }
    if (!switches.is_enabled(name, static_cast<std::size_t>(connector - m_follow_connectors.data()))) {
        return "a disabled connector";
    }
    out_connector = *connector;
    return nullptr;
}

void Level_kits::gather_attachments(const Scene_root& scene_root, std::vector<Attached_piece>& out_attached) const
{
    out_attached.clear();
    // Each uid once: a target is looked up in the map, and a uid of more than one node is ambiguous.
    std::unordered_map<std::string_view, Uid_lookup> uids;
    scene_root.get_scene().for_each_node(
        [&out_attached, &uids](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
            Uid_lookup& lookup = uids[std::string_view{node->get_gltf_uid()}];
            if (lookup.count == 0) {
                lookup.node = node;
            }
            ++lookup.count;
            if (node->has_local_value(Kit_piece::attachment_property.get())) {
                const std::optional<Kit_attachment> attachment = read_kit_attachment(*node);
                if (attachment.has_value()) {
                    out_attached.push_back(Attached_piece{.node = node, .attachment = attachment.value()});
                }
            }
            return true;
        }
    );
    // A piece whose own uid names more than one node (a file imported twice) is inert, as a copy is.
    std::erase_if(out_attached, [&uids](const Attached_piece& piece) -> bool { return uids[std::string_view{piece.node->get_gltf_uid()}].count > 1; });
    std::vector<Kit_connector> connectors;
    // The local connector of the end named, enabled; the dormant reason otherwise.
    const auto resolve = [this, &connectors](const erhe::scene::Node& node, const std::string& name, Kit_connector& out_connector) -> const char* {
        const std::optional<Kit_piece_data> record = read_kit_piece(node);
        Kit_parameter_values values;
        if (!record.has_value() || !read_piece_values(record.value(), values).empty()) {
            return "a record that does not read";
        }
        get_shape_connectors(values, connectors);
        const Kit_connector* const connector = find_connector(connectors, name);
        if (connector == nullptr) {
            return "a connector the shape does not have";
        }
        const std::size_t index = static_cast<std::size_t>(connector - connectors.data());
        if (!get_connector_switches(node, connectors).is_enabled(name, index)) {
            return "a disabled connector";
        }
        out_connector = *connector;
        return nullptr;
    };
    for (Attached_piece& piece : out_attached) {
        const std::unordered_map<std::string_view, Uid_lookup>::const_iterator target = uids.find(std::string_view{piece.attachment.target});
        if (target == uids.end()) {
            piece.link_dormant = c_target_missing;
        } else if (target->second.count > 1) {
            piece.link_dormant = c_target_ambiguous;
        } else {
            piece.target = target->second.node;
            if (find_locking_kit_layout(*piece.node) || find_locking_kit_layout(*piece.target)) {
                piece.link_dormant = c_locked_layout;
            }
        }
        piece.own_dormant = resolve(*piece.node, piece.attachment.connector, piece.own);
        if (piece.target) {
            piece.target_dormant = resolve(*piece.target, piece.attachment.target_connector, piece.target_local);
        }
    }
}

void Level_kits::follow_attachments(
    Scene_root&                                               scene_root,
    const std::span<Attached_piece>                           attached,
    const std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
    const std::span<const std::shared_ptr<erhe::scene::Node>> changed_pieces,
    std::vector<Follow_move>&                                 out_moves,
    std::vector<Follow_detach>* const                         out_detached
)
{
    m_follow_user.clear();
    m_follow_dirty.clear();
    m_follow_joined.clear();
    for (const std::shared_ptr<erhe::scene::Node>& node : moved_nodes) {
        m_follow_user.push_back(node.get());
        m_follow_dirty.push_back(node.get());
    }
    for (const std::shared_ptr<erhe::scene::Node>& node : changed_pieces) {
        m_follow_dirty.push_back(node.get());
    }
    erhe::scene::Scene& scene = scene_root.get_scene();
    scene.update_node_transforms();

    // A piece the user moved (at or below a moved node) is never joined again: the user placed it.
    // The followers: a changed attached piece (its own connector moved) and, transitively, every piece whose target is
    // at or below a moved or changed node or an earlier follower.
    for (Attached_piece& piece : attached) {
        piece.done     = false;
        piece.affected = (piece.dormant() == nullptr) && !is_at_or_below(m_follow_user, piece.node.get()) &&
                         contains(m_follow_dirty, piece.node.get()) && !contains(m_follow_user, piece.node.get());
    }
    for (Attached_piece& piece : attached) {
        if (piece.affected) {
            m_follow_dirty.push_back(piece.node.get());
        }
    }
    bool marked = true;
    while (marked) {
        marked = false;
        for (Attached_piece& piece : attached) {
            if (piece.affected || (piece.dormant() != nullptr) || is_at_or_below(m_follow_user, piece.node.get()) ||
                !is_at_or_below(m_follow_dirty, piece.target.get())) {
                continue;
            }
            piece.affected = true;
            m_follow_dirty.push_back(piece.node.get());
            marked = true;
        }
    }

    const auto rejoin = [&](Attached_piece& piece) {
        piece.done = true;
        erhe::scene::Node& node = *piece.node;
        const std::optional<Kit_join_pose> motion = attachment_motion(node, piece.own, *piece.target, piece.target_local, piece.attachment.twist_steps, piece.attachment.gap);
        if (!motion.has_value() || is_identity_motion(motion.value())) {
            return;
        }
        const std::shared_ptr<erhe::scene::Node> parent = node.get_parent_node();
        const std::optional<glm::mat4> parent_from_node = moved_parent_from_node(
            motion.value(), node.world_from_node(), parent ? parent->node_from_world() : glm::mat4{1.0f}
        );
        if (!parent_from_node.has_value()) {
            log_scene->trace("Kit piece '{}' does not follow its target: the joined pose would need a sheared local transform", node.get_name());
            return;
        }
        const erhe::scene::Trs_transform before = node.parent_from_node_transform();
        const erhe::scene::Trs_transform after  = make_joined_local_transform(parent_from_node.value(), before.get_scale());
        Follow_move* const known = [&]() -> Follow_move* {
            for (Follow_move& move : out_moves) {
                if (move.node.get() == &node) {
                    return &move;
                }
            }
            return nullptr;
        }();
        if (known == nullptr) {
            out_moves.push_back(Follow_move{.node = piece.node, .before = before, .after = after, .xform_op_stack_before = node.copy_xform_op_stack()});
        } else {
            known->after = after;
        }
        node.set_parent_from_node(after);
        node.update_world_from_node();
        m_follow_joined.push_back(&node);
        // The follower's descendants (and their connectors) move with it before the next follower is joined.
        scene.update_node_transforms();
    };

    // Dependency order: a follower is joined once no pending follower is at or above its target or above the piece.
    std::size_t pending = static_cast<std::size_t>(std::count_if(attached.begin(), attached.end(), [](const Attached_piece& piece) -> bool { return piece.affected; }));
    while (pending > 0) {
        bool progress = false;
        for (Attached_piece& piece : attached) {
            if (!piece.affected || piece.done) {
                continue;
            }
            const bool blocked = std::any_of(
                attached.begin(), attached.end(),
                [&piece](const Attached_piece& other) -> bool {
                    if (!other.affected || other.done || (&other == &piece)) {
                        return false;
                    }
                    // The pending follower moves the target, or the piece itself as one of its descendants.
                    for (const erhe::scene::Node* i = piece.target.get(); i != nullptr; i = i->get_parent_node().get()) {
                        if (i == other.node.get()) {
                            return true;
                        }
                    }
                    for (const erhe::scene::Node* i = piece.node->get_parent_node().get(); i != nullptr; i = i->get_parent_node().get()) {
                        if (i == other.node.get()) {
                            return true;
                        }
                    }
                    return false;
                }
            );
            if (blocked) {
                continue;
            }
            rejoin(piece);
            --pending;
            progress = true;
        }
        if (!progress) {
            // A cycle (raw record writes) has no order: its first piece is joined, each piece once.
            for (Attached_piece& piece : attached) {
                if (piece.affected && !piece.done) {
                    rejoin(piece);
                    --pending;
                    break;
                }
            }
        }
    }
    if (out_detached == nullptr) {
        return;
    }
    // The pieces this change moved without joining them: moved by the user, or riding below a follower.
    for (Attached_piece& piece : attached) {
        if (piece.done) {
            continue;
        }
        const bool by_user = is_at_or_below(m_follow_user, piece.node.get());
        const bool riding  = !by_user && is_at_or_below(m_follow_joined, piece.node.get());
        if (!by_user && !riding) {
            continue;
        }
        if (piece.dormant() == nullptr) {
            if (is_on_join(*piece.node, piece.own, *piece.target, piece.target_local, piece.attachment)) {
                continue;
            }
        } else if (piece.target && (is_at_or_below(m_follow_user, piece.target.get()) || is_at_or_below(m_follow_joined, piece.target.get()))) {
            continue; // dormant, moved together with its target
        }
        out_detached->push_back(
            Follow_detach{
                .node   = piece.node,
                .reason = by_user ? "the change moved it off its join" : "it rode below a follower off its own join"
            }
        );
    }
}

void Level_kits::build_attachment_follow_ups(
    Scene_root&                                               scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
    const std::span<const std::shared_ptr<erhe::scene::Node>> changed_pieces,
    const Kit_snap_pair* const                                snap,
    std::vector<std::shared_ptr<Operation>>&                  out_operations
)
{
    std::vector<Attached_piece> attached;
    gather_attachments(scene_root, attached);
    std::vector<Follow_move>   moves;
    std::vector<Follow_detach> detached;
    if (!attached.empty()) {
        follow_attachments(scene_root, attached, moved_nodes, changed_pieces, moves, &detached);
    }
    for (Follow_move& move : moves) {
        log_scene->trace("Kit piece '{}' follows its target", move.node->get_name());
        out_operations.push_back(
            std::make_shared<Node_transform_operation>(
                Node_transform_operation::Parameters{
                    .node                    = move.node,
                    .parent_from_node_before = move.before,
                    .parent_from_node_after  = move.after,
                    .xform_op_stack_before   = std::move(move.xform_op_stack_before)
                }
            )
        );
    }
    // The snapped release, judged at the poses being recorded (the followers joined): it attaches instead of detaching.
    const std::optional<Kit_attachment> snapped = (snap != nullptr) ? find_snap_attachment(*snap) : std::optional<Kit_attachment>{};
    for (const Follow_detach& detach : detached) {
        if (snapped.has_value() && (detach.node == snap->piece)) {
            continue;
        }
        log_scene->info("Kit piece '{}' detached: {}", detach.node->get_name(), detach.reason);
        std::shared_ptr<Operation> operation = make_attachment_operation(detach.node, std::nullopt);
        if (operation) {
            out_operations.push_back(std::move(operation));
        }
    }
    if (snapped.has_value()) {
        std::shared_ptr<Operation> operation = make_attachment_operation(snap->piece, snapped);
        if (operation) {
            log_scene->info("Kit piece '{}' attached to '{}' by the snapped release", snap->piece->get_name(), snap->target->get_name());
            out_operations.push_back(std::move(operation));
        }
    }
}

void Level_kits::restore_follow_preview(Follow_preview& preview)
{
    Scene_root* const scene_root = preview.scene_root;
    for (auto i = preview.moves.rbegin(), end = preview.moves.rend(); i != end; ++i) {
        i->node->restore_local_transform(i->before, i->xform_op_stack_before);
        i->node->update_world_from_node();
    }
    if ((scene_root != nullptr) && !preview.moves.empty()) {
        scene_root->get_scene().update_node_transforms();
    }
    preview.scene_root = nullptr;
    preview.moved.clear();
    preview.changed.clear();
    preview.attached.clear();
    preview.moves.clear();
}

void Level_kits::step_move_follow_preview()
{
    Follow_preview& preview = m_move_follow_preview;
    if ((preview.scene_root == nullptr) || preview.attached.empty()) {
        return;
    }
    follow_attachments(*preview.scene_root, preview.attached, preview.moved, {}, preview.moves, nullptr);
}

void Level_kits::step_parameter_follow_preview(const std::shared_ptr<erhe::scene::Node>& node, const Kit_parameter_values& values)
{
    Follow_preview& preview = m_parameter_follow_preview;
    Scene_root* const scene_root = dynamic_cast<Scene_root*>(node->get_item_host());
    if (scene_root == nullptr) {
        return;
    }
    if ((preview.scene_root != nullptr) && ((preview.scene_root != scene_root) || preview.changed.empty() || (preview.changed.front() != node))) {
        restore_follow_preview(preview);
    }
    // Gathered once at the drag's begin; each step reads only the dragged piece's connectors from the step's values.
    if (preview.scene_root == nullptr) {
        if (!has_attachments(*scene_root)) {
            return;
        }
        gather_attachments(*scene_root, preview.attached);
        if (preview.attached.empty()) {
            return;
        }
        preview.scene_root       = scene_root;
        preview.changed.push_back(node);
        preview.changed_switches = read_piece_connector_switches(*node);
    }
    for (Attached_piece& piece : preview.attached) {
        if (piece.node == node) {
            piece.own_dormant = resolve_connector_of_values(values, preview.changed_switches, piece.attachment.connector, piece.own);
        }
        if (piece.target == node) {
            piece.target_dormant = resolve_connector_of_values(values, preview.changed_switches, piece.attachment.target_connector, piece.target_local);
        }
    }
    follow_attachments(*scene_root, preview.attached, {}, preview.changed, preview.moves, nullptr);
}

} // namespace editor
