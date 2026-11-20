// Step 41 of doc/plans/level_tools_implementation.md: persistent connector attachments (doc/editor/level_tools.md "Attachments").

#include "level/level_kits.hpp"
#include "level/kit_connector_snap.hpp"
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
#include <chrono>
#include <cmath>
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

// A join of two wall connectors (C3.16) or two floor connectors (shared corners): moved by the fits and the corner
// sharing, never rigidly.
[[nodiscard]] auto is_wall_join(const Kit_connector& own, const Kit_connector& target) -> bool
{
    return (own.types.contains(Kit_connector_type::wall) && target.types.contains(Kit_connector_type::wall)) ||
           (own.types.contains(Kit_connector_type::floor) && target.types.contains(Kit_connector_type::floor));
}

// The one "joined" rule per connection kind (K6.7): a wall or floor join holds while its two connectors share a place (1 mm;
// their directions may differ, a room corner); any other join while the piece's pose is the join's (twist and gap).
[[nodiscard]] auto is_joined_now(const erhe::scene::Node& node, const Kit_connector& own, const erhe::scene::Node& target, const Kit_connector& target_local, const Kit_attachment& attachment) -> bool
{
    if (is_wall_join(own, target_local)) {
        const std::optional<Kit_connector> a = transform_connector(own, node.world_from_node());
        const std::optional<Kit_connector> b = transform_connector(target_local, target.world_from_node());
        return a.has_value() && b.has_value() && (glm::distance(a->position, b->position) <= 1.0e-3f);
    }
    return is_on_join(node, own, target, target_local, attachment);
}

} // anonymous namespace

void Level_kits::get_connected_groups(const Scene_root& scene_root, std::vector<std::vector<std::shared_ptr<erhe::scene::Node>>>& out_groups) const
{
    out_groups.clear();
    std::vector<Attached_piece> attached;
    gather_attachments(scene_root, attached);
    // The ends in scene order, each with the index of its group's representative (union by first index).
    std::vector<std::shared_ptr<erhe::scene::Node>> ends;
    scene_root.get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
        const bool is_end = std::any_of(attached.begin(), attached.end(), [&node](const Attached_piece& piece) {
            return piece.target && ((piece.node == node) || (piece.target == node));
        });
        if (is_end) {
            ends.push_back(node);
        }
        return true;
    });
    std::vector<std::size_t> parent(ends.size());
    for (std::size_t i = 0; i < parent.size(); ++i) {
        parent[i] = i;
    }
    const auto index_of = [&ends](const std::shared_ptr<erhe::scene::Node>& node) {
        return static_cast<std::size_t>(std::find(ends.begin(), ends.end(), node) - ends.begin());
    };
    const auto root_of = [&parent](std::size_t i) {
        while (parent[i] != i) {
            i = parent[i];
        }
        return i;
    };
    for (const Attached_piece& piece : attached) {
        if (!piece.target) {
            continue;
        }
        const std::size_t a = root_of(index_of(piece.node));
        const std::size_t b = root_of(index_of(piece.target));
        parent[std::max(a, b)] = std::min(a, b);
    }
    std::vector<std::size_t> roots;
    for (std::size_t i = 0; i < ends.size(); ++i) {
        const std::size_t root = root_of(i);
        const std::size_t group = static_cast<std::size_t>(std::find(roots.begin(), roots.end(), root) - roots.begin());
        if (group == roots.size()) {
            roots.push_back(root);
            out_groups.emplace_back();
        }
        out_groups[group].push_back(ends[i]);
    }
}

auto Level_kits::is_connection_joined(const erhe::scene::Node& owner, const Kit_attachment& attachment, const erhe::scene::Node& target) const -> bool
{
    std::vector<Kit_connector> own_frames;
    std::vector<Kit_connector> target_frames;
    if (!get_local_connectors(owner, own_frames, Kit_connector_set::all).empty() || !get_local_connectors(target, target_frames, Kit_connector_set::all).empty()) {
        return false;
    }
    const Kit_connector* const own          = find_connector(own_frames, attachment.connector);
    const Kit_connector* const target_local = find_connector(target_frames, attachment.target_connector);
    return (own != nullptr) && (target_local != nullptr) && is_joined_now(owner, *own, target, *target_local, attachment);
}

auto is_host_end_of_join(const erhe::scene::Node& node, const Kit_connector& own, const erhe::scene::Node& other, const Kit_connector& other_connector) -> bool
{
    const bool floor_of_wall = is_floor_layout(node) && !is_floor_layout(other) &&
                               own.types.contains(Kit_connector_type::wall_floor) && other_connector.types.contains(Kit_connector_type::wall_floor);
    return floor_of_wall || is_rail_host_of(own, other_connector);
}

auto make_reversed_attachment(const Kit_attachment& attachment) -> Kit_attachment
{
    return Kit_attachment{
        .owner            = attachment.target,
        .target           = attachment.owner,
        .connector        = attachment.target_connector,
        .target_connector = attachment.connector,
        .twist_steps      = attachment.twist_steps,
        .gap              = attachment.gap
    };
}

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

auto Level_kits::make_join_connection(const std::shared_ptr<erhe::scene::Node>& node, const Kit_parameter_values& values, const Kit_connect& connect) const -> Kit_new_connection
{
    const Kit_attachment attachment = make_connect_attachment(*node, connect);
    std::vector<Kit_connector> own_frames;
    std::vector<Kit_connector> target_frames;
    get_shape_connectors(values, own_frames);
    const Kit_connector* const own    = find_connector(own_frames, connect.connector);
    const Kit_connector* const target = (connect.target && get_local_connectors(*connect.target, target_frames, Kit_connector_set::all).empty())
        ? find_connector(target_frames, connect.target_connector)
        : nullptr;
    if ((own != nullptr) && (target != nullptr) && is_host_end_of_join(*node, *own, *connect.target, *target)) {
        return Kit_new_connection{.node = connect.target, .attachment = make_reversed_attachment(attachment), .target = node};
    }
    return Kit_new_connection{.node = node, .attachment = attachment, .target = connect.target};
}

auto Level_kits::make_attachment_operation(const std::shared_ptr<erhe::scene::Node>& node, const std::optional<Kit_attachment>& attachment) -> std::shared_ptr<Operation>
{
    // K6.1: the attachment replaces the connection of its connector and keeps the others; none removes every connection.
    std::vector<Kit_attachment> attachments;
    if (attachment.has_value()) {
        attachments = read_kit_attachments(*node);
        std::erase_if(attachments, [&attachment](const Kit_attachment& other) { return other.connector == attachment.value().connector; });
        attachments.push_back(attachment.value());
    }
    return make_attachments_operation(node, attachments);
}

auto Level_kits::make_connection_removal_operation(const std::shared_ptr<erhe::scene::Node>& node, const std::string_view connector) -> std::shared_ptr<Operation>
{
    std::vector<Kit_attachment> attachments = read_kit_attachments(*node);
    std::erase_if(attachments, [connector](const Kit_attachment& other) { return other.connector == connector; });
    return make_attachments_operation(node, attachments);
}

auto Level_kits::make_attachments_operation(const std::shared_ptr<erhe::scene::Node>& node, const std::vector<Kit_attachment>& attachments) -> std::shared_ptr<Operation>
{
    const erhe::property::Dependency_property& property = Kit_piece::attachment_property.get();
    const std::optional<erhe::property::Local_state> before = node->read_local_state(property);
    const std::optional<std::string> text_before = node->read_local_value(Kit_piece::attachment_property);
    const std::string text = make_attachments_text(attachments);
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
    return {};
}

void Level_kits::get_attachment_statuses(const erhe::scene::Node& node, std::vector<Kit_attachment_status>& out_statuses) const
{
    out_statuses.clear();
    for (const Kit_attachment& attachment : read_kit_attachments(node)) {
        out_statuses.push_back(get_attachment_status(node, attachment));
    }
}

auto Level_kits::get_attachment_status(const erhe::scene::Node& node, const Kit_attachment& attachment) const -> Kit_attachment_status
{
    Kit_attachment_status status;
    status.attachment = attachment;
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
    const std::array<std::shared_ptr<erhe::scene::Node>, 1> nodes{node};
    return detach_pieces(scene_root, nodes);
}

auto Level_kits::detach_pieces(Scene_root& scene_root, const std::span<const std::shared_ptr<erhe::scene::Node>> nodes) -> Kit_detach_result
{
    Kit_detach_result result;
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        if (!node || (node->get_item_host() != &scene_root)) {
            result.error = node ? fmt::format("node '{}' is not in scene '{}'", node->get_name(), scene_root.get_name()) : std::string{"no node"};
            return result;
        }
        if (!carries_kit_piece(*node) && !is_floor_layout(*node)) {
            result.error = fmt::format("'{}' is neither a kit piece nor a floor layout", node->get_name());
            return result;
        }
    }
    // C9.1: every connection with an end in the nodes, the records of other pieces naming them included; one entry, no flow.
    std::vector<std::string_view> uids;
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        uids.push_back(node->get_gltf_uid());
    }
    const auto is_detached = [&uids](const std::string_view uid) { return std::find(uids.begin(), uids.end(), uid) != uids.end(); };
    std::vector<std::shared_ptr<Operation>> operations;
    scene_root.get_scene().for_each_node(
        [&](const std::shared_ptr<erhe::scene::Node>& other) -> bool {
            if (!other->has_local_value(Kit_piece::attachment_property.get())) {
                return true;
            }
            const bool own = std::find(nodes.begin(), nodes.end(), other) != nodes.end();
            std::vector<Kit_attachment> kept = own ? std::vector<Kit_attachment>{} : read_kit_attachments(*other);
            std::erase_if(kept, [&is_detached](const Kit_attachment& attachment) { return is_detached(attachment.target); });
            // A record copied from another node (not its own) is removed too.
            std::shared_ptr<Operation> operation = make_attachments_operation(other, kept);
            if (operation) {
                operations.push_back(std::move(operation));
            }
            return true;
        }
    );
    if (operations.empty()) {
        return result;
    }
    m_context.operation_stack->execute_now(std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)}));
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        log_scene->info("Kit piece '{}' detached", node->get_name());
    }
    result.changed = true;
    return result;
}

auto Level_kits::remove_connection(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node, const std::string& connector) -> Kit_remove_connection_result
{
    Kit_remove_connection_result result;
    if (!node || (node->get_item_host() != &scene_root)) {
        result.error = "the node is not in the scene";
        return result;
    }
    std::shared_ptr<Operation> operation = make_connection_removal_operation(node, connector);
    if (!operation) {
        // Recorded on the other end: that end's entry naming this node's connector.
        const std::string uid = node->get_gltf_uid();
        scene_root.get_scene().for_each_node(
            [&](const std::shared_ptr<erhe::scene::Node>& other) -> bool {
                if (!other->has_local_value(Kit_piece::attachment_property.get())) {
                    return true;
                }
                std::vector<Kit_attachment> kept = read_kit_attachments(*other);
                const std::size_t before = kept.size();
                std::erase_if(kept, [&](const Kit_attachment& a) { return (a.target == uid) && (a.target_connector == connector); });
                if (kept.size() != before) {
                    operation = make_attachments_operation(other, kept);
                    return false;
                }
                return true;
            }
        );
    }
    if (!operation) {
        return result;
    }
    m_context.operation_stack->execute_now(operation);
    result.changed = true;
    return result;
}

auto Level_kits::find_connection_at(const Scene_root& scene_root, const erhe::scene::Node& node, const std::string_view connector) const -> std::shared_ptr<erhe::scene::Node>
{
    const std::optional<Kit_connection_end_at> end = find_connection_end(scene_root, node, connector);
    return end.has_value() ? end.value().node : std::shared_ptr<erhe::scene::Node>{};
}

auto Level_kits::find_connection_end(const Scene_root& scene_root, const erhe::scene::Node& node, const std::string_view connector) const -> std::optional<Kit_connection_end_at>
{
    for (const Kit_attachment& attachment : read_kit_attachments(node)) {
        if (attachment.connector == connector) {
            std::shared_ptr<erhe::scene::Node> target = find_uid(scene_root, attachment.target).node;
            if (!target) {
                break; // its target is gone: this record holds the connector no longer
            }
            return Kit_connection_end_at{.node = std::move(target), .connector = attachment.target_connector};
        }
    }
    std::optional<Kit_connection_end_at> found;
    const std::string_view uid = node.get_gltf_uid();
    scene_root.get_scene().for_each_node(
        [&](const std::shared_ptr<erhe::scene::Node>& other) -> bool {
            if (!other->has_local_value(Kit_piece::attachment_property.get())) {
                return true;
            }
            for (const Kit_attachment& attachment : read_kit_attachments(*other)) {
                if ((attachment.target == uid) && (attachment.target_connector == connector)) {
                    found = Kit_connection_end_at{.node = other, .connector = attachment.connector};
                    return false;
                }
            }
            return true;
        }
    );
    return found;
}

auto Level_kits::check_one_connection_per_connector(
    const Scene_root&        scene_root,
    const erhe::scene::Node& node,
    const std::string&       connector,
    const erhe::scene::Node& target,
    const std::string&       target_connector
) const -> std::string
{
    const auto same_end = [](const std::optional<Kit_connection_end_at>& end, const erhe::scene::Node& other, const std::string& other_connector) {
        return (end.value().node.get() == &other) && (end.value().connector == other_connector);
    };
    const std::optional<Kit_connection_end_at> at_target = find_connection_end(scene_root, target, target_connector);
    if (at_target.has_value() && !same_end(at_target, node, connector)) {
        return fmt::format(
            "connector '{}' of '{}' is connected to '{}' already (its connector '{}'; detach it first)",
            target_connector, target.get_name(), at_target.value().node->get_name(), at_target.value().connector
        );
    }
    // The node's own record of the connector is replaced by the join; a record of another piece naming it is not.
    const std::vector<Kit_attachment> own = read_kit_attachments(node);
    const bool own_record = std::any_of(own.begin(), own.end(), [&connector](const Kit_attachment& a) { return a.connector == connector; });
    const std::optional<Kit_connection_end_at> at_node = own_record ? std::optional<Kit_connection_end_at>{} : find_connection_end(scene_root, node, connector);
    if (at_node.has_value() && !same_end(at_node, target, target_connector)) {
        return fmt::format(
            "connector '{}' of '{}' is connected to '{}' already (its connector '{}'; detach it first)",
            connector, node.get_name(), at_node.value().node->get_name(), at_node.value().connector
        );
    }
    return {};
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
    // K6.2: a connector connected to another piece, or to another connector of the pair, takes no second connection.
    if (!check_one_connection_per_connector(*scene_root, *snap.piece, snap.connector, *snap.target, snap.target_connector).empty()) {
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
        if (!is_join_twist_allowed(own->types, target_local->types, twist)) {
            continue;
        }
        const std::optional<Kit_join_pose> motion = attachment_motion(*snap.piece, *own, *snap.target, *target_local, twist, 0.0f);
        if (motion.has_value() && is_identity_motion(motion.value())) {
            const Kit_attachment attachment{
                .owner            = snap.piece->get_gltf_uid(),
                .target           = snap.target->get_gltf_uid(),
                .connector        = snap.connector,
                .target_connector = snap.target_connector,
                .twist_steps      = twist,
                .gap              = 0.0f
            };
            // K6.1, K13: a floor snapped onto a wall records the connection on the wall, a host snapped onto a rider on the rider.
            return is_host_end_of_join(*snap.piece, *own, *snap.target, *target_local) ? make_reversed_attachment(attachment) : attachment;
        }
    }
    return std::nullopt;
}

void Level_kits::append_replace_connection_drops(const Scene_root& scene_root, Kit_replace_result& result, std::vector<std::shared_ptr<Operation>>& out_operations) const
{
    // The replaced pieces' connectors at their new values.
    std::vector<std::vector<Kit_connector>> new_connectors(result.pieces.size());
    for (std::size_t i = 0, end = result.pieces.size(); i < end; ++i) {
        if (result.pieces[i].replaced) {
            get_shape_connectors(result.pieces[i].values, new_connectors[i]);
        }
    }
    const auto find_replaced = [&result](const erhe::scene::Node* const node) -> std::optional<std::size_t> {
        for (std::size_t i = 0, end = result.pieces.size(); i < end; ++i) {
            if (result.pieces[i].replaced && (result.pieces[i].mesh.get() == node)) {
                return i;
            }
        }
        return {};
    };
    const auto find_replaced_uid = [&result](const std::string_view uid) -> std::optional<std::size_t> {
        for (std::size_t i = 0, end = result.pieces.size(); i < end; ++i) {
            if (result.pieces[i].replaced && (result.pieces[i].mesh->get_gltf_uid() == uid)) {
                return i;
            }
        }
        return {};
    };
    std::vector<Kit_connector> own_scratch;
    std::vector<Kit_connector> other_scratch;
    const auto connector_of = [&](const erhe::scene::Node& node, const std::string_view name, std::vector<Kit_connector>& scratch) -> const Kit_connector* {
        const std::optional<std::size_t> index = find_replaced(&node);
        if (index.has_value()) {
            return find_connector(new_connectors[index.value()], name);
        }
        if (!get_local_connectors(node, scratch, Kit_connector_set::all).empty()) {
            return nullptr;
        }
        return find_connector(scratch, name);
    };
    scene_root.get_scene().for_each_node(
        [&](const std::shared_ptr<erhe::scene::Node>& owner) -> bool {
            if (!owner->has_local_value(Kit_piece::attachment_property.get())) {
                return true;
            }
            std::vector<Kit_attachment> entries = read_kit_attachments(*owner);
            const std::optional<std::size_t> owner_index = find_replaced(owner.get());
            const std::size_t before = entries.size();
            std::erase_if(
                entries,
                [&](const Kit_attachment& entry) -> bool {
                    const std::optional<std::size_t> target_index = find_replaced_uid(entry.target);
                    if (!owner_index.has_value() && !target_index.has_value()) {
                        return false;
                    }
                    const Uid_lookup target = find_uid(scene_root, entry.target);
                    const Kit_connector* const own   = connector_of(*owner, entry.connector, own_scratch);
                    const Kit_connector* const other = (target.count == 1) ? connector_of(*target.node, entry.target_connector, other_scratch) : nullptr;
                    std::string reason;
                    if (owner_index.has_value() && (own == nullptr)) {
                        reason = fmt::format("the new shape has no connector '{}'", entry.connector);
                    } else if (target_index.has_value() && (other == nullptr)) {
                        reason = fmt::format("the new shape has no connector '{}'", entry.target_connector);
                    } else if ((own != nullptr) && (other != nullptr) && !is_compatible(*own, *other)) {
                        reason = fmt::format("the connector types of '{}' and '{}' no longer meet", entry.connector, entry.target_connector);
                    } else {
                        return false;
                    }
                    if (owner_index.has_value()) {
                        result.pieces[owner_index.value()].dropped_connections.push_back(
                            Kit_dropped_connection{.connector = entry.connector, .other = target.node, .other_connector = entry.target_connector, .reason = reason}
                        );
                    }
                    if (target_index.has_value()) {
                        result.pieces[target_index.value()].dropped_connections.push_back(
                            Kit_dropped_connection{.connector = entry.target_connector, .other = owner, .other_connector = entry.connector, .reason = reason}
                        );
                    }
                    ++result.dropped_connection_count;
                    log_scene->info("Replace dropped the connection of '{}' {} and {}: {}", owner->get_name(), entry.connector, entry.target_connector, reason);
                    return true;
                }
            );
            if (entries.size() != before) {
                std::shared_ptr<Operation> operation = make_attachments_operation(owner, entries);
                if (operation) {
                    out_operations.push_back(std::move(operation));
                }
            }
            return true;
        }
    );
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
    const Kit_snap_pair* const                                snap,
    const std::span<const std::shared_ptr<erhe::scene::Node>> requesting,
    const std::span<const Kit_new_connection>                 new_connections
) -> std::shared_ptr<Operation>
{
    const bool snapped = (snap != nullptr) && snap->piece && snap->target;
    // With Auto-connect on, any change of pieces may connect them, attachments in the scene or not (C5).
    const bool may_connect = get_effective_auto_connect() && (!moved_nodes.empty() || !changed_pieces.empty());
    if (!snapped && requesting.empty() && !may_connect && !has_attachments(scene_root)) {
        return {};
    }
    return std::make_shared<Kit_attachment_follow_operation>(
        std::vector<std::shared_ptr<erhe::scene::Node>>{moved_nodes.begin(), moved_nodes.end()},
        std::vector<std::shared_ptr<erhe::scene::Node>>{changed_pieces.begin(), changed_pieces.end()},
        snapped ? std::optional<Kit_snap_pair>{*snap} : std::optional<Kit_snap_pair>{},
        std::vector<std::shared_ptr<erhe::scene::Node>>{requesting.begin(), requesting.end()},
        std::vector<Kit_new_connection>{new_connections.begin(), new_connections.end()},
        m_flow_overrides,
        m_auto_connect_excluded
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
                // A record list that does not read gives none; its write and its load name it (K6.2).
                for (Kit_attachment& attachment : read_kit_attachments(*node)) {
                    out_attached.push_back(Attached_piece{.node = node, .attachment = std::move(attachment)});
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
        // A kit piece's or a floor layout's frames (K6).
        if (!get_local_connectors(node, connectors, Kit_connector_set::all).empty()) {
            return "a record that does not read";
        }
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
    m_follow_flow_mask = get_effective_flow_mask();
    // A wall standing on a floor layout's edge, joined over wall-floor, or a rider on its host, joined over rail (C10.5),
    // with the type let through the mask: the piece goes back onto its join when the user moved it alone.
    const auto returns_to_join = [](const Attached_piece& piece, const Kit_connector_types mask) -> bool {
        if (!piece.target) {
            return false;
        }
        const bool wall_on_floor = is_floor_layout(*piece.target) && mask.contains(Kit_connector_type::wall_floor) &&
                                   piece.own.types.contains(Kit_connector_type::wall_floor) && piece.target_local.types.contains(Kit_connector_type::wall_floor);
        return wall_on_floor || (mask.contains(Kit_connector_type::rail) && is_rail_host_of(piece.target_local, piece.own));
    };
    for (Attached_piece& piece : attached) {
        piece.done     = false;
        piece.joined   = false;
        piece.rigid    = true;
        piece.affected = (piece.dormant() == nullptr) && !is_at_or_below(m_follow_user, piece.node.get()) &&
                         contains(m_follow_dirty, piece.node.get()) && !contains(m_follow_user, piece.node.get());
        // C3.16: an entry a wall fit of this change set is neither joined again nor detached.
        if (std::find(m_fitted_pairs.begin(), m_fitted_pairs.end(), std::pair<const erhe::scene::Node*, const erhe::scene::Node*>{piece.node.get(), piece.target.get()}) != m_fitted_pairs.end()) {
            piece.affected = false;
            piece.done     = true;
        }
        // A join of two wall connectors moves only by the fits (C3.16, design K12); a join of types the flow mask switched
        // off is not followed (C3.9).
        if (!is_rigid_join(piece, m_follow_flow_mask)) {
            piece.affected = false;
            piece.rigid    = false;
        } else if ((out_detached != nullptr) && (piece.dormant() == nullptr) && returns_to_join(piece, m_follow_flow_mask) &&
                   is_at_or_below(m_follow_user, piece.node.get()) && !is_at_or_below(m_follow_user, piece.target.get())) {
            // K5.5 (C3.15, L20): a wall the user moved goes back onto its floor edge, upright, once the floor fitted it; a
            // rider goes back onto its host's rail (C10.5, the host does not fit); a live parameter drag's following (no
            // out_detached) leaves it where it is.
            piece.affected = true;
        }
    }
    for (Attached_piece& piece : attached) {
        if (piece.affected) {
            m_follow_dirty.push_back(piece.node.get());
        }
    }
    mark_followers(attached, m_follow_user, m_follow_dirty);

    Kit_flow_report* const report = (out_detached != nullptr) ? get_flow_report() : nullptr;
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
        piece.joined = true;
        if (report != nullptr) {
            report->add_response(Kit_flow_response{.piece = make_flow_piece(node), .kind = Kit_flow_response_kind::follow, .from = make_flow_piece(*piece.target)});
        }
        // The follower's descendants (and their connectors) move with it before the next follower is joined.
        scene.update_node_transforms();
    };

    // Dependency order: a follower is joined once no pending follower is at or above its target or above the piece; a
    // cycle without such a follower is ordered by Kit_follow_order (C3.7).
    m_follow_entries.clear();
    for (const Attached_piece& piece : attached) {
        const bool origin = std::any_of(
            changed_pieces.begin(), changed_pieces.end(), [&piece](const std::shared_ptr<erhe::scene::Node>& node) { return node.get() == piece.node.get(); }
        );
        m_follow_entries.push_back(
            Kit_follow_entry{.node = piece.node.get(), .affected = piece.affected && !piece.done, .origin = origin}
        );
    }
    m_follow_order.plan(
        m_follow_entries,
        [&attached](const std::size_t i, const std::size_t j) -> bool {
            const Attached_piece& piece = attached[i];
            const Attached_piece& other = attached[j];
            // The pending follower moves the target, or the piece itself as one of its descendants.
            for (const erhe::scene::Node* k = piece.target.get(); k != nullptr; k = k->get_parent_node().get()) {
                if (k == other.node.get()) {
                    return true;
                }
            }
            for (const erhe::scene::Node* k = piece.node->get_parent_node().get(); k != nullptr; k = k->get_parent_node().get()) {
                if (k == other.node.get()) {
                    return true;
                }
            }
            return false;
        }
    );
    for (const Kit_follow_step& step : m_follow_order.get_steps()) {
        Attached_piece& piece = attached[step.entry];
        if (step.action == Kit_follow_action::join) {
            rejoin(piece);
        } else {
            // Left where it is: kept, and reported apart where its connectors no longer meet.
            piece.done = true;
            log_scene->trace("Kit piece '{}' connector '{}' is not joined again: a cycle of connections", piece.node->get_name(), piece.attachment.connector);
        }
    }
    if (out_detached == nullptr) {
        return;
    }
    // Whether the piece's target was joined, directly or through a chain of joined followers, to the piece or a node above it.
    const auto follows_back = [](const std::span<Attached_piece> entries, const Attached_piece& piece) -> bool {
        const erhe::scene::Node* current = piece.target.get();
        for (std::size_t step = 0; (current != nullptr) && (step <= entries.size()); ++step) {
            for (const erhe::scene::Node* i = piece.node.get(); i != nullptr; i = i->get_parent_node().get()) {
                if (i == current) {
                    return true;
                }
            }
            // The entry by which the current node, or a node above it, followed.
            const erhe::scene::Node* next = nullptr;
            for (const erhe::scene::Node* i = current; (i != nullptr) && (next == nullptr); i = i->get_parent_node().get()) {
                for (const Attached_piece& other : entries) {
                    if (other.joined && (other.node.get() == i)) {
                        next = other.target.get();
                        break;
                    }
                }
            }
            current = next;
        }
        return false;
    };
    // The pieces this change moved without joining them: moved by the user, or riding below a follower. A join no end
    // follows (two wall or floor connectors, a type the flow mask switched off) is judged the same whichever end moved and
    // whichever end records it (C3.9, C3.14, K6.1).
    for (Attached_piece& piece : attached) {
        if (piece.done) {
            continue;
        }
        const bool either_end = !piece.rigid && piece.target;
        const bool by_user = is_at_or_below(m_follow_user, piece.node.get()) || (either_end && is_at_or_below(m_follow_user, piece.target.get()));
        const bool riding  = !by_user &&
                             (is_at_or_below(m_follow_joined, piece.node.get()) || (either_end && is_at_or_below(m_follow_joined, piece.target.get())));
        if (!by_user && !riding) {
            continue;
        }
        // C3.14: the other piece followed this piece in this change (a cycle closing on it): kept, apart or not (C3.7).
        if ((piece.dormant() == nullptr) && piece.rigid && follows_back(attached, piece)) {
            continue;
        }
        if (piece.dormant() == nullptr) {
            if (is_joined_now(*piece.node, piece.own, *piece.target, piece.target_local, piece.attachment)) {
                continue;
            }
        } else if (piece.target && (is_at_or_below(m_follow_user, piece.target.get()) || is_at_or_below(m_follow_joined, piece.target.get()))) {
            continue; // dormant, moved together with its target
        }
        out_detached->push_back(
            Follow_detach{
                .node      = piece.node,
                .target    = piece.target,
                .connector = piece.attachment.connector,
                .reason    = by_user ? "the change moved it off its join" : "it rode below a follower off its own join"
            }
        );
    }
}

auto Level_kits::is_rigid_join(const Attached_piece& piece, const Kit_connector_types mask) -> bool
{
    return !is_wall_join(piece.own, piece.target_local) && piece.own.types.intersects(Kit_connector_types{.bits = piece.target_local.types.bits & mask.bits});
}

void Level_kits::mark_followers(
    const std::span<Attached_piece>              attached,
    const std::vector<const erhe::scene::Node*>& user,
    std::vector<const erhe::scene::Node*>&       dirty
)
{
    bool marked = true;
    while (marked) {
        marked = false;
        for (Attached_piece& piece : attached) {
            if (piece.done || !piece.rigid || piece.affected || (piece.dormant() != nullptr) || is_at_or_below(user, piece.node.get()) ||
                !is_at_or_below(dirty, piece.target.get())) {
                continue;
            }
            piece.affected = true;
            dirty.push_back(piece.node.get());
            marked = true;
        }
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
    // Every removal and the snapped attach of one node go into one list, written once (K6.0).
    std::vector<std::pair<std::shared_ptr<erhe::scene::Node>, std::vector<Kit_attachment>>> lists;
    const auto list_of = [&lists](const std::shared_ptr<erhe::scene::Node>& node) -> std::vector<Kit_attachment>& {
        for (std::pair<std::shared_ptr<erhe::scene::Node>, std::vector<Kit_attachment>>& entry : lists) {
            if (entry.first == node) {
                return entry.second;
            }
        }
        lists.emplace_back(node, read_kit_attachments(*node));
        return lists.back().second;
    };
    for (const Follow_detach& detach : detached) {
        // The snapped pair's connectors take the snapped record (find_snap_attachment() refused a pair held elsewhere).
        if (snapped.has_value() && (((detach.node == snap->piece) && (detach.connector == snap->connector)) ||
                                    ((detach.node == snap->target) && (detach.connector == snap->target_connector)))) {
            continue;
        }
        log_scene->log(flow_log_level(spdlog::level::info), "Kit piece '{}' connector '{}' detached: {}", detach.node->get_name(), detach.connector, detach.reason);
        std::vector<Kit_attachment>& list = list_of(detach.node);
        if (Kit_flow_report* const report = get_flow_report(); report != nullptr) {
            for (const Kit_attachment& a : list) {
                if ((a.connector == detach.connector) && detach.target) {
                    report->add_connection(
                        report->removed_connections,
                        Kit_flow_connection{.piece = make_flow_piece(*detach.node), .connector = a.connector, .other = make_flow_piece(*detach.target), .other_connector = a.target_connector}
                    );
                }
            }
        }
        std::erase_if(list, [&detach](const Kit_attachment& a) { return a.connector == detach.connector; });
    }
    if (snapped.has_value()) {
        // The end that records it (K6.1) replaces its record of the connector; the other end's record of the same pair goes.
        const bool on_piece = (snapped.value().owner == snap->piece->get_gltf_uid());
        const std::shared_ptr<erhe::scene::Node>& holder = on_piece ? snap->piece : snap->target;
        const std::shared_ptr<erhe::scene::Node>& other  = on_piece ? snap->target : snap->piece;
        std::vector<Kit_attachment>& list = list_of(holder);
        std::erase_if(list, [&snapped](const Kit_attachment& a) { return a.connector == snapped.value().connector; });
        list.push_back(snapped.value());
        std::vector<Kit_attachment>& other_list = list_of(other);
        std::erase_if(other_list, [&snapped](const Kit_attachment& a) {
            return (a.connector == snapped.value().target_connector) && (a.target == snapped.value().owner) && (a.target_connector == snapped.value().connector);
        });
        log_scene->log(flow_log_level(spdlog::level::info), "Kit piece '{}' attached to '{}' by the snapped release", snap->piece->get_name(), snap->target->get_name());
        report_new_connection(Kit_new_connection{.node = holder, .attachment = snapped.value(), .target = other});
    }
    // C3.7: the connections of the change's pieces that it leaves apart, kept (a cycle that does not close).
    if (Kit_flow_report* const report = get_flow_report(); report != nullptr) {
        for (const Attached_piece& piece : attached) {
            if ((piece.dormant() != nullptr) || !piece.target ||
                (!is_at_or_below(m_follow_dirty, piece.node.get()) && !is_at_or_below(m_follow_dirty, piece.target.get()))) {
                continue;
            }
            const bool removed = std::any_of(detached.begin(), detached.end(), [&piece](const Follow_detach& detach) {
                return (detach.node == piece.node) && (detach.connector == piece.attachment.connector);
            });
            if (removed) {
                continue;
            }
            if (!is_joined_now(*piece.node, piece.own, *piece.target, piece.target_local, piece.attachment)) {
                report->add_connection(
                    report->apart_connections,
                    Kit_flow_connection{
                        .piece           = make_flow_piece(*piece.node),
                        .connector       = piece.attachment.connector,
                        .other           = make_flow_piece(*piece.target),
                        .other_connector = piece.attachment.target_connector
                    }
                );
            }
        }
    }
    for (const std::pair<std::shared_ptr<erhe::scene::Node>, std::vector<Kit_attachment>>& entry : lists) {
        std::shared_ptr<Operation> operation = make_attachments_operation(entry.first, entry.second);
        if (operation) {
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

void Level_kits::collect_flow_moved(
    const Scene_root&                                         scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Node>> nodes,
    std::vector<std::shared_ptr<erhe::scene::Node>>&          out_moved,
    std::vector<Kit_connection_end>&                          out_joined
) const
{
    out_moved.clear();
    out_joined.clear();
    if (!has_attachments(scene_root)) {
        return;
    }
    std::vector<Attached_piece> attached;
    gather_attachments(scene_root, attached);
    const Kit_connector_types mask = get_effective_flow_mask();
    std::vector<const erhe::scene::Node*> user;
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        if (node) {
            user.push_back(node.get());
        }
    }
    std::vector<const erhe::scene::Node*> dirty = user;
    const auto add_dirty = [&dirty](const erhe::scene::Node* node) {
        if (!contains(dirty, node)) {
            dirty.push_back(node);
        }
    };
    for (const Attached_piece& piece : attached) {
        if ((piece.dormant() != nullptr) || !piece.target) {
            continue;
        }
        // C3.15 (build_floor_fits()): the floor under a wall-floor join of a moved wall fits it.
        if (mask.contains(Kit_connector_type::wall_floor) && contains(user, piece.node.get()) && carries_kit_piece(*piece.node) &&
            is_floor_edge_join(*piece.target, piece.attachment.target_connector)) {
            add_dirty(piece.target.get());
        }
        // C3.16 (build_wall_fits()): a wall joined end to end to a moved wall fits it.
        if (mask.contains(Kit_connector_type::wall)) {
            const std::shared_ptr<erhe::scene::Node>* const wall = get_wall_fit_wall(
                piece, [&user](const erhe::scene::Node* node) { return is_at_or_below(user, node); }, attached
            );
            if (wall != nullptr) {
                add_dirty(wall->get());
            }
        }
    }
    // The followers of the moved nodes and of the fitted pieces, by record direction (follow_attachments()).
    for (Attached_piece& piece : attached) {
        piece.done     = false;
        piece.affected = false;
        piece.rigid    = is_rigid_join(piece, mask);
    }
    mark_followers(attached, user, dirty);
    const auto add_moved = [&](const std::shared_ptr<erhe::scene::Node>& node) {
        if (node && contains(dirty, node.get()) && !is_at_or_below(user, node.get()) &&
            std::none_of(out_moved.begin(), out_moved.end(), [&node](const std::shared_ptr<erhe::scene::Node>& n) { return n == node; })) {
            out_moved.push_back(node);
        }
    };
    for (const Attached_piece& piece : attached) {
        add_moved(piece.node);
        add_moved(piece.target);
    }
    // A partner of the drag's pieces that does not move keeps its joined connector occupied: that one connector.
    for (const Attached_piece& piece : attached) {
        if (!piece.target) {
            continue;
        }
        const bool own   = is_at_or_below(user, piece.node.get());
        const bool other = is_at_or_below(user, piece.target.get());
        if (own == other) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Node>& partner = own ? piece.target : piece.node;
        if (!is_at_or_below(dirty, partner.get())) {
            out_joined.push_back(Kit_connection_end{.node = partner.get(), .connector = own ? piece.attachment.target_connector : piece.attachment.connector});
        }
    }
}

void Level_kits::read_flow_preview_tool_poses()
{
    Flow_preview& preview = m_flow_preview;
    preview.tool_poses.clear();
    for (std::size_t i = 0, end = preview.moved.size(); i < end; ++i) {
        const erhe::scene::Trs_transform& current = preview.moved[i]->parent_from_node_transform();
        // A node the run moved (K5.5) that still stands at the run's output was not written by the drag since.
        const bool by_run = preview.ran && (i < preview.outputs.size()) && (preview.outputs[i] != preview.inputs[i]) && (current == preview.outputs[i]);
        preview.tool_poses.push_back(by_run ? preview.inputs[i] : current);
    }
}

namespace {

[[nodiscard]] auto is_same_pose(const erhe::scene::Trs_transform& a, const erhe::scene::Trs_transform& b) -> bool
{
    // A held snap puts the moved connector on the target's; the pose it computes differs in the last bits only, which
    // grow with the magnitude (a few float ulps of it).
    constexpr float c_relative_tolerance = 1.0e-6f;
    const glm::mat4& m = a.get_matrix();
    const glm::mat4& n = b.get_matrix();
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            const float scale = std::max({1.0f, std::abs(m[column][row]), std::abs(n[column][row])});
            if (std::abs(m[column][row] - n[column][row]) > (c_relative_tolerance * scale)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto is_same_snap(const Kit_snap_pair* const snap, const std::optional<Kit_snap_pair>& held) -> bool
{
    if ((snap == nullptr) || !held.has_value()) {
        return (snap == nullptr) && !held.has_value();
    }
    return (snap->piece == held->piece) && (snap->target == held->target) && (snap->connector == held->connector) &&
           (snap->target_connector == held->target_connector);
}

} // anonymous namespace

auto Level_kits::is_flow_preview_held(const Kit_snap_pair* const snap) const -> bool
{
    const Flow_preview& preview = m_flow_preview;
    if (!preview.ran || (preview.inputs.size() != preview.tool_poses.size()) || !is_same_snap(snap, preview.snap)) {
        return false;
    }
    for (std::size_t i = 0, end = preview.inputs.size(); i < end; ++i) {
        if (!is_same_pose(preview.tool_poses[i], preview.inputs[i])) {
            return false;
        }
    }
    return true;
}

void Level_kits::set_flow_preview_poses(const std::span<const erhe::scene::Trs_transform> poses)
{
    Flow_preview& preview = m_flow_preview;
    bool changed = false;
    for (std::size_t i = 0, end = std::min(preview.moved.size(), poses.size()); i < end; ++i) {
        erhe::scene::Node& node = *preview.moved[i];
        if (node.parent_from_node_transform() != poses[i]) {
            node.set_parent_from_node(poses[i]);
            changed = true;
        }
    }
    if (changed && (preview.scene_root != nullptr)) {
        preview.scene_root->get_scene().update_node_transforms();
    }
}

void Level_kits::undo_flow_preview_run()
{
    Flow_preview& preview = m_flow_preview;
    if (preview.applied && preview.run) {
        hold_selection();
        m_flow_previewing      = true;
        m_move_flow_previewing = true;
        preview.run->undo(m_context);
        m_flow_previewing      = false;
        m_move_flow_previewing = false;
        restore_held_selection();
    }
    preview.applied = false;
    set_flow_preview_poses(preview.tool_poses);
}

void Level_kits::run_flow_preview(const Kit_snap_pair* const snap)
{
    Flow_preview& preview = m_flow_preview;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const bool snapped = (snap != nullptr) && snap->piece && snap->target;
    // The release's follow operation of the step's poses and pair, executed as the release's first execute runs it.
    std::shared_ptr<Operation> run = make_attachment_follow_operation(*preview.scene_root, preview.moved, {}, snapped ? snap : nullptr);
    hold_selection();
    m_flow_previewing      = true;
    m_move_flow_previewing = true;
    if (run) {
        run->execute(m_context);
    }
    std::string refusal;
    if (run && run->is_refused()) {
        // K0.3: the last valid run is shown again; the moved nodes stay where the drag has them.
        refusal = run->get_error();
        if (preview.run) {
            preview.run->execute(m_context);
            preview.applied = true;
        }
    } else {
        preview.run     = std::move(run);
        preview.applied = static_cast<bool>(preview.run);
    }
    m_flow_previewing      = false;
    m_move_flow_previewing = false;
    restore_held_selection();
    if (!refusal.empty()) {
        set_flow_preview_poses(preview.tool_poses);
        ++m_flow_preview_state.refused_steps;
        log_scene->trace("Kit flow preview keeps its last valid step: {}", refusal);
    }
    m_flow_preview_state.refusal = std::move(refusal);
    preview.inputs = preview.tool_poses;
    preview.outputs.clear();
    for (const std::shared_ptr<erhe::scene::Node>& node : preview.moved) {
        preview.outputs.push_back(node->parent_from_node_transform());
    }
    if (snapped) {
        preview.snap = *snap;
    } else {
        preview.snap.reset();
    }
    preview.ran = true;
    if (snapped) {
        m_flow_preview_state.snap = Kit_flow_preview_pair{
            .piece_id         = static_cast<std::uint64_t>(snap->piece->get_id()),
            .piece            = snap->piece->get_name(),
            .connector        = snap->connector,
            .target_id        = static_cast<std::uint64_t>(snap->target->get_id()),
            .target           = snap->target->get_name(),
            .target_connector = snap->target_connector
        };
    } else {
        m_flow_preview_state.snap.reset();
    }
    ++m_flow_preview_state.runs;
    log_scene->trace(
        "flow preview step: {} moved nodes, snapped {}: {:.3f} ms", preview.moved.size(), snapped,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
    );
}

void Level_kits::end_flow_preview()
{
    Flow_preview& preview = m_flow_preview;
    if (preview.scene_root == nullptr) {
        return;
    }
    // The drag's last poses, which the release records: a snap after the last step (transform_selection) wrote them.
    read_flow_preview_tool_poses();
    undo_flow_preview_run();
    forget_flow_preview();
}

void Level_kits::forget_flow_preview()
{
    Flow_preview& preview = m_flow_preview;
    preview.scene_root = nullptr;
    preview.moved.clear();
    preview.inputs.clear();
    preview.outputs.clear();
    preview.tool_poses.clear();
    preview.snap.reset();
    preview.run.reset();
    preview.applied = false;
    preview.ran     = false;
    m_flow_preview_state.active = false;
}

auto Level_kits::get_flow_preview_state() const -> const Kit_flow_preview_state&
{
    return m_flow_preview_state;
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
