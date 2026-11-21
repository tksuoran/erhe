// Surface attachments: a kit piece snapped onto another becomes its child (doc/editor/level_tools.md "Surface attachments").

#include "level/level_kits.hpp"
#include "level/kit_layout.hpp"
#include "level/kit_piece.hpp"
#include "level/kit_piece_operation.hpp"
#include "level/kit_surface_attachment.hpp"
#include "level/level_queries.hpp"

#include "editor_log.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/property_set_operation.hpp"
#include "scene/scene_root.hpp"

#include "erhe_math/math_util.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace editor {

namespace {

// S6.2: a rest point this close to a face of the support still rests on it; also the window that finds a snapped rest.
constexpr float c_surface_rest_distance = 1.0e-4f;

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

[[nodiscard]] auto get_world_pose(const erhe::scene::Node& node) -> Surface_pose
{
    const erhe::scene::Trs_transform world = node.world_from_node_transform();
    return Surface_pose{.position = world.get_translation(), .rotation = world.get_rotation(), .scale = world.get_scale()};
}

// The options of a search for the rest the node holds now: the snap's own, searching a window of c_surface_rest_distance.
[[nodiscard]] auto make_rest_query(const glm::vec3& direction, const Surface_orient orient, const Surface_snap_rest rest) -> Surface_options
{
    Surface_options options = make_surface_options(Surface_snap::on, orient, direction, c_surface_rest_distance);
    options.rest = rest;
    return options;
}

} // anonymous namespace

auto Level_kits::is_flow_previewing() const -> bool
{
    return m_flow_previewing;
}

auto Level_kits::has_surface_attachments(const Scene_root& scene_root) -> bool
{
    bool found = false;
    scene_root.get_scene().for_each_node(
        [&found](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
            found = node->has_local_value(Kit_piece::surface_attachment_property.get());
            return !found;
        }
    );
    return found;
}

auto Level_kits::find_surface_support(const erhe::scene::Mesh* const mesh) -> std::shared_ptr<erhe::scene::Node>
{
    if ((mesh == nullptr) || (mesh->get_item_host() == nullptr)) {
        return {};
    }
    // S2.3: a kit piece, or the mesh a kit layout generates (a floor's slab, a staircase).
    if (!carries_kit_piece(*mesh) && read_layout_generated_source(*mesh).empty()) {
        return {};
    }
    return std::const_pointer_cast<erhe::scene::Node>(std::dynamic_pointer_cast<const erhe::scene::Node>(mesh->shared_from_this()));
}

auto Level_kits::get_surface_home(const erhe::scene::Node& node) -> std::shared_ptr<erhe::Hierarchy>
{
    std::shared_ptr<erhe::Hierarchy> parent = node.get_parent().lock();
    while (parent) {
        const erhe::scene::Node* const parent_node = dynamic_cast<const erhe::scene::Node*>(parent.get());
        if ((parent_node == nullptr) || (!carries_kit_piece(*parent_node) && !carries_kit_layout(*parent_node))) {
            return parent;
        }
        std::shared_ptr<erhe::Hierarchy> next = parent->get_parent().lock();
        if (!next) {
            return parent;
        }
        parent = std::move(next);
    }
    return {};
}

auto Level_kits::has_connection(const Scene_root& scene_root, const erhe::scene::Node& node) -> bool
{
    if (!read_kit_attachments(node).empty()) {
        return true;
    }
    const std::string uid = node.get_gltf_uid();
    bool found = false;
    scene_root.get_scene().for_each_node(
        [&found, &node, &uid](const std::shared_ptr<erhe::scene::Node>& other) -> bool {
            if ((other.get() == &node) || !other->has_local_value(Kit_piece::attachment_property.get())) {
                return true;
            }
            for (const Kit_attachment& attachment : read_kit_attachments(*other)) {
                if (attachment.target == uid) {
                    found = true;
                }
            }
            return !found;
        }
    );
    return found;
}

auto Level_kits::make_surface_record(
    const erhe::math::Aabb&  piece_bounds,
    const glm::mat4&         world_from_node,
    const erhe::scene::Node& support,
    const glm::vec3&         direction,
    const Surface_orient     orient,
    const Surface_snap_rest  rest
) -> std::optional<Kit_surface_attachment>
{
    const erhe::scene::Mesh* const support_mesh = dynamic_cast<const erhe::scene::Mesh*>(&support);
    if (support_mesh == nullptr) {
        return {};
    }
    // The pose and the direction in the support's node space, where the record lives.
    const glm::mat4                  node_from_world = support.node_from_world();
    const erhe::scene::Trs_transform local{node_from_world * world_from_node};
    const glm::vec3                  local_direction = glm::mat3{node_from_world} * direction;
    const float                      length          = glm::length(local_direction);
    if (!std::isfinite(length) || !(length > 0.0f)) {
        return {};
    }
    const glm::vec3 unit_direction = local_direction / length;
    const std::optional<glm::dvec3> position = make_surface_attachment_position(
        mesh_local_bounds(*support_mesh),
        piece_bounds,
        Surface_pose{.position = local.get_translation(), .rotation = local.get_rotation(), .scale = local.get_scale()},
        unit_direction, orient, rest
    );
    if (!position.has_value()) {
        return {};
    }
    return Kit_surface_attachment{
        .target    = support.get_gltf_uid(),
        .position  = position.value(),
        .direction = unit_direction,
        .orient    = orient,
        .rest      = rest
    };
}

auto Level_kits::make_surface_record_operation(const std::shared_ptr<erhe::scene::Node>& node, const std::optional<Kit_surface_attachment>& record) -> std::shared_ptr<Operation>
{
    const erhe::property::Dependency_property& property = Kit_piece::surface_attachment_property.get();
    const std::optional<std::string> text_before = node->read_local_value(Kit_piece::surface_attachment_property);
    const std::string text = record.has_value() ? make_surface_attachment_text(record.value()) : std::string{};
    if (text == text_before.value_or(std::string{})) {
        return {};
    }
    // No record is no local value.
    return std::make_shared<Property_set_operation>(
        node, property, node->read_local_state(property),
        text.empty() ? std::optional<erhe::property::Local_state>{} : std::optional<erhe::property::Local_state>{erhe::property::Property_value{text}}
    );
}

auto Level_kits::make_reparent_surface_follow_up(const erhe::Hierarchy& child, const erhe::Hierarchy* const new_parent) -> std::shared_ptr<Operation>
{
    const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(&child);
    if ((node == nullptr) || !node->has_local_value(Kit_piece::surface_attachment_property.get())) {
        return {};
    }
    const std::optional<Kit_surface_attachment> record = read_kit_surface_attachment(*node);
    const erhe::scene::Node* const parent_node = dynamic_cast<const erhe::scene::Node*>(new_parent);
    if (record.has_value() && (parent_node != nullptr) && (parent_node->get_gltf_uid() == record.value().target)) {
        return {};
    }
    // S6.5: under another parent the record is inert; it goes in the same entry.
    const std::shared_ptr<erhe::scene::Node> shared_node =
        std::const_pointer_cast<erhe::scene::Node>(std::dynamic_pointer_cast<const erhe::scene::Node>(node->shared_from_this()));
    return make_surface_record_operation(shared_node, std::nullopt);
}

void Level_kits::append_surface_detach(const std::shared_ptr<erhe::scene::Node>& node, std::vector<std::shared_ptr<Operation>>& out_operations)
{
    if (!node || !node->has_local_value(Kit_piece::surface_attachment_property.get())) {
        return;
    }
    std::shared_ptr<Operation> removal = make_surface_record_operation(node, std::nullopt);
    if (removal) {
        out_operations.push_back(std::move(removal));
    }
    // P1, S6.3: to its home with its subtree, keeping its world transform (Item_parent_change_operation keeps it).
    const std::shared_ptr<erhe::Hierarchy> home   = get_surface_home(*node);
    const std::shared_ptr<erhe::Hierarchy> parent = node->get_parent().lock();
    if (home && (home != parent)) {
        out_operations.push_back(std::make_shared<Item_parent_change_operation>(home, node, std::shared_ptr<erhe::Hierarchy>{}, std::shared_ptr<erhe::Hierarchy>{}));
    }
}

void Level_kits::build_surface_attachment_follow_ups(
    Scene_root&                                               scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
    const std::span<const Kit_surface_snap>                   surface_snaps,
    const std::span<const std::shared_ptr<erhe::scene::Node>> connected,
    std::vector<std::shared_ptr<Operation>>&                  out_operations
)
{
    // The nodes the change moved and snapped; their meshes and the meshes below them are no surface (S2.2), as in the snap.
    std::vector<const erhe::scene::Node*> moved;
    for (const std::shared_ptr<erhe::scene::Node>& node : moved_nodes) {
        if (node) {
            moved.push_back(node.get());
        }
    }
    for (const Kit_surface_snap& snap : surface_snaps) {
        if (snap.node) {
            moved.push_back(snap.node.get());
        }
    }
    std::vector<const erhe::scene::Mesh*> excluded;
    for (const erhe::scene::Node* const node : moved) {
        static_cast<void>(
            node->for_each_const<erhe::scene::Mesh>(
                [&excluded](const erhe::scene::Mesh& mesh) -> bool {
                    excluded.push_back(&mesh);
                    return true;
                }
            )
        );
    }

    std::vector<const erhe::scene::Node*> handled;
    const auto detach = [this, &out_operations](const std::shared_ptr<erhe::scene::Node>& node, const char* why) {
        const Kit_surface_attachment_status status = get_surface_attachment_status(*node);
        const std::size_t before = out_operations.size();
        append_surface_detach(node, out_operations);
        if (out_operations.size() != before) {
            log_scene->info(
                "Kit piece '{}' detached from '{}': {}", node->get_name(), status.target ? status.target->get_name() : std::string{"(missing)"}, why
            );
        }
    };
    const auto attach = [&out_operations](const std::shared_ptr<erhe::scene::Node>& node, const std::shared_ptr<erhe::scene::Node>& support, const Kit_surface_attachment& record) {
        if (node->get_parent().lock() != support) {
            out_operations.push_back(std::make_shared<Item_parent_change_operation>(support, node, std::shared_ptr<erhe::Hierarchy>{}, std::shared_ptr<erhe::Hierarchy>{}));
        }
        std::shared_ptr<Operation> write = make_surface_record_operation(node, record);
        if (write) {
            out_operations.push_back(std::move(write));
        }
    };
    const auto judge = [&](const std::shared_ptr<erhe::scene::Node>& node, const Surface_options* const snap_options) {
        if (!node || contains(handled, node.get()) || (node->get_item_host() != &scene_root)) {
            return;
        }
        handled.push_back(node.get());
        const Kit_surface_attachment_status status = get_surface_attachment_status(*node);
        const std::shared_ptr<erhe::scene::Node> parent = node->get_parent_node();
        // S3.1: moved with its support, it rides the scene graph.
        if (is_surface_attached(status.state) && parent && is_at_or_below(moved, parent.get())) {
            return;
        }
        // S2.4: connectors come first.
        if (has_connection(scene_root, *node)) {
            if (status.state != Kit_surface_attachment_state::none) {
                detach(node, "it holds a connector connection");
            }
            return;
        }
        const erhe::scene::Mesh* const mesh = dynamic_cast<const erhe::scene::Mesh*>(node.get());
        // S2.6: a kit layout's element records nothing (its layout snaps it at every generation).
        const bool layout_element = parent && carries_kit_layout(*parent);
        const bool snapped_attach =
            (snap_options != nullptr) && (snap_options->attach == Surface_attach::on) && any_surface_option(*snap_options) && (mesh != nullptr) && !layout_element;
        if (snapped_attach) {
            // S2.1, S2.2, S6.1: the support is the mesh owning the face the snapped rest lies on.
            const Surface_placement rest = find_surface_placement(
                scene_root, mesh_local_bounds(*mesh), get_world_pose(*node),
                make_rest_query(snap_options->direction, snap_options->orient, snap_options->rest), excluded
            );
            const std::shared_ptr<erhe::scene::Node> support = rest.surface_found ? find_surface_support(rest.support_mesh) : std::shared_ptr<erhe::scene::Node>{};
            const std::optional<Kit_surface_attachment> record = support
                ? make_surface_record(mesh_local_bounds(*mesh), node->world_from_node(), *support, snap_options->direction, snap_options->orient, snap_options->rest)
                : std::optional<Kit_surface_attachment>{};
            if (record.has_value()) {
                attach(node, support, record.value());
                return;
            }
            // S2.5: a snap onto no support detaches; an inert record goes (S1.2).
            if (is_surface_attached(status.state)) {
                detach(node, "the snap found no kit piece under it");
            } else if (status.state == Kit_surface_attachment_state::inert) {
                std::shared_ptr<Operation> removal = make_surface_record_operation(node, std::nullopt);
                if (removal) {
                    out_operations.push_back(std::move(removal));
                }
            }
            return;
        }
        if (status.state == Kit_surface_attachment_state::inert) {
            // S1.2: the first change that reads a record inert removes it; the node stays where it is.
            std::shared_ptr<Operation> removal = make_surface_record_operation(node, std::nullopt);
            if (removal) {
                out_operations.push_back(std::move(removal));
            }
            return;
        }
        if (!is_surface_attached(status.state) || (mesh == nullptr)) {
            return;
        }
        // S6.2: still at rest on a face of its support, it keeps the attachment at its new place; else S6.3.
        const erhe::scene::Mesh* const support_mesh = dynamic_cast<const erhe::scene::Mesh*>(status.target.get());
        const Kit_surface_attachment& record    = status.attachment;
        const glm::vec3               direction = glm::mat3{status.target->world_from_node()} * record.direction;
        const float                   length    = glm::length(direction);
        if ((support_mesh != nullptr) && std::isfinite(length) && (length > 0.0f)) {
            const glm::vec3 world_direction = direction / length;
            const std::array<const erhe::scene::Mesh*, 1> only{support_mesh};
            const std::array<const erhe::scene::Mesh*, 1> self{mesh};
            const Surface_placement rest = find_surface_placement(
                scene_root, mesh_local_bounds(*mesh), get_world_pose(*node),
                make_rest_query(world_direction, record.orient, record.rest), self, only
            );
            if (rest.surface_found && (std::abs(rest.distance) <= c_surface_rest_distance)) {
                const std::optional<Kit_surface_attachment> placed = make_surface_record(mesh_local_bounds(*mesh), node->world_from_node(), *status.target, world_direction, record.orient, record.rest);
                if (placed.has_value()) {
                    attach(node, status.target, placed.value());
                }
                return;
            }
        }
        detach(node, "it was moved off its support");
    };

    for (const Kit_surface_snap& snap : surface_snaps) {
        judge(snap.node, &snap.options);
    }
    for (const std::shared_ptr<erhe::scene::Node>& node : moved_nodes) {
        if (node && node->has_local_value(Kit_piece::surface_attachment_property.get())) {
            judge(node, nullptr);
        }
    }
    // S2.4: a piece that took part in a new connection of the change gives up its surface attachment.
    for (const std::shared_ptr<erhe::scene::Node>& node : connected) {
        if (node && !contains(handled, node.get()) && node->has_local_value(Kit_piece::surface_attachment_property.get()) && has_connection(scene_root, *node)) {
            handled.push_back(node.get());
            detach(node, "it holds a connector connection");
        }
    }
}

auto Level_kits::compute_surface_carry(
    Scene_root&                   scene_root,
    const erhe::scene::Mesh&      piece,
    const erhe::scene::Mesh&      support,
    const Kit_surface_attachment& record
) const -> std::optional<Kit_surface_carry>
{
    const erhe::math::Aabb           support_bounds = mesh_local_bounds(support);
    const erhe::math::Aabb           piece_bounds   = mesh_local_bounds(piece);
    const erhe::scene::Trs_transform local          = piece.parent_from_node_transform();
    const std::optional<glm::vec3>   translation    = carry_surface_attachment(record, support_bounds, piece_bounds, local.get_rotation(), local.get_scale());
    if (!translation.has_value()) {
        return {};
    }
    // The carry rounds in double: a translation equal to the present one within 1e-6 m (relative) is the present one.
    const glm::vec3 present = local.get_translation();
    const glm::vec3 delta   = glm::abs(translation.value() - present);
    const glm::vec3 allowed = 1.0e-6f * glm::max(glm::vec3{1.0f}, glm::abs(present));
    erhe::scene::Trs_transform carried = local;
    if ((delta.x > allowed.x) || (delta.y > allowed.y) || (delta.z > allowed.z)) {
        carried.set_translation(translation.value());
    }
    // S3.3: rest again along the record's direction, against the support's faces alone, the settings' distance either way.
    const glm::mat4 world_from_support = support.world_from_node();
    const glm::vec3 direction          = glm::mat3{world_from_support} * record.direction;
    const float     length             = glm::length(direction);
    if (!std::isfinite(length) || !(length > 0.0f)) {
        return {};
    }
    const glm::vec3            world_direction = direction / length;
    const erhe::scene::Trs_transform start{world_from_support * carried.get_matrix()};
    Surface_options options = make_surface_options(Surface_snap::on, record.orient, world_direction, get_surface_settings().forward_distance);
    options.rest = record.rest;
    const std::array<const erhe::scene::Mesh*, 1> only{&support};
    const Surface_pose start_pose{.position = start.get_translation(), .rotation = start.get_rotation(), .scale = start.get_scale()};
    const Surface_placement rest = find_surface_placement(scene_root, piece_bounds, start_pose, options, {}, only);
    Kit_surface_carry result{.local = carried, .record = {}};
    if (!rest.surface_found) {
        // S3.4: the carried pose stays and the proportion is kept, so a later change of the support can rest it again.
        if (record.on_surface) {
            Kit_surface_attachment off = record;
            off.on_surface = false;
            result.record = off;
        }
        return result;
    }
    const bool rested_as_carried = (rest.pose.position == start_pose.position) && (rest.pose.rotation == start_pose.rotation);
    if (rested_as_carried) {
        if (!record.on_surface) {
            Kit_surface_attachment on = record;
            on.on_surface = true;
            result.record = on;
        }
        return result;
    }
    // The rest moved the piece: its place is written again from the rested pose.
    const glm::mat4 world_from_node =
        erhe::math::create_translation<float>(rest.pose.position) * glm::mat4_cast(rest.pose.rotation) * erhe::math::create_scale<float>(rest.pose.scale);
    erhe::scene::Trs_transform rested{support.node_from_world() * world_from_node};
    result.local  = rested;
    result.record = make_surface_record(piece_bounds, world_from_node, support, world_direction, record.orient, record.rest);
    return result;
}

void Level_kits::build_surface_carry_follow_ups(
    Scene_root&                                               scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Node>> changed_pieces,
    const std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
    const Kit_surface_carry_record                            record_mode,
    std::vector<std::shared_ptr<Operation>>&                  out_operations,
    std::vector<const erhe::scene::Node*>&                    out_carried
)
{
    if (changed_pieces.empty()) {
        return;
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> changed;
    for (const std::shared_ptr<erhe::scene::Node>& node : changed_pieces) {
        if (node && (node->get_item_host() == &scene_root) && (std::find(changed.begin(), changed.end(), node) == changed.end())) {
            changed.push_back(node);
        }
    }
    // S4.3: parents before children.
    std::stable_sort(changed.begin(), changed.end(), [](const std::shared_ptr<erhe::scene::Node>& lhs, const std::shared_ptr<erhe::scene::Node>& rhs) -> bool {
        return lhs->get_depth() < rhs->get_depth();
    });
    const auto is_moved = [moved_nodes](const erhe::scene::Node* node) -> bool {
        for (const std::shared_ptr<erhe::scene::Node>& moved : moved_nodes) {
            if (moved.get() == node) {
                return true;
            }
        }
        return false;
    };
    const auto carry = [&](const std::shared_ptr<erhe::scene::Node>& node, const Kit_surface_attachment_status& status) {
        out_carried.push_back(node.get());
        const erhe::scene::Mesh* const piece   = dynamic_cast<const erhe::scene::Mesh*>(node.get());
        const erhe::scene::Mesh* const support = dynamic_cast<const erhe::scene::Mesh*>(status.target.get());
        if ((piece == nullptr) || (support == nullptr)) {
            return;
        }
        const std::optional<Kit_surface_carry> result = compute_surface_carry(scene_root, *piece, *support, status.attachment);
        if (!result.has_value()) {
            return;
        }
        std::vector<std::shared_ptr<Operation>> operations;
        const erhe::scene::Trs_transform before = node->parent_from_node_transform();
        if (result.value().local != before) {
            operations.push_back(
                std::make_shared<Node_transform_operation>(
                    Node_transform_operation::Parameters{
                        .node                    = node,
                        .parent_from_node_before = before,
                        .parent_from_node_after  = result.value().local,
                        .xform_op_stack_before   = node->copy_xform_op_stack()
                    }
                )
            );
        }
        if ((record_mode == Kit_surface_carry_record::write) && result.value().record.has_value()) {
            std::shared_ptr<Operation> write = make_surface_record_operation(node, result.value().record);
            if (write) {
                operations.push_back(std::move(write));
            }
        }
        // Executed now: the next carries rest on this piece's new pose (S4.3).
        for (std::shared_ptr<Operation>& operation : operations) {
            operation->execute(m_context);
            out_operations.push_back(std::move(operation));
        }
        if (!operations.empty() || (result.value().local != before)) {
            scene_root.get_scene().update_node_transforms();
        }
    };
    const auto carried = [&out_carried](const erhe::scene::Node* node) -> bool {
        return std::find(out_carried.begin(), out_carried.end(), node) != out_carried.end();
    };
    for (const std::shared_ptr<erhe::scene::Node>& support : changed) {
        if (dynamic_cast<const erhe::scene::Mesh*>(support.get()) == nullptr) {
            continue;
        }
        // A copy: a carry changes no child list, but the walk must not depend on it.
        const std::vector<std::shared_ptr<erhe::Hierarchy>> children = support->get_children();
        for (const std::shared_ptr<erhe::Hierarchy>& child : children) {
            const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
            if (!node || carried(node.get()) || is_moved(node.get()) || !node->has_local_value(Kit_piece::surface_attachment_property.get())) {
                continue;
            }
            const Kit_surface_attachment_status status = get_surface_attachment_status(*node);
            if (is_surface_attached(status.state)) {
                carry(node, status);
            }
        }
    }
    // S6.4: a changed attached piece rests again on its support.
    for (const std::shared_ptr<erhe::scene::Node>& node : changed) {
        if (carried(node.get()) || is_moved(node.get()) || !node->has_local_value(Kit_piece::surface_attachment_property.get())) {
            continue;
        }
        const Kit_surface_attachment_status status = get_surface_attachment_status(*node);
        if (is_surface_attached(status.state)) {
            carry(node, status);
        }
    }
}

void Level_kits::step_surface_carry_preview(Scene_root& scene_root, const erhe::scene::Mesh& dragged, std::vector<Follow_move>& io_moves)
{
    const auto carry = [&](const std::shared_ptr<erhe::scene::Node>& node) {
        const erhe::scene::Mesh* const piece = dynamic_cast<const erhe::scene::Mesh*>(node.get());
        if ((piece == nullptr) || !node->has_local_value(Kit_piece::surface_attachment_property.get())) {
            return;
        }
        const Kit_surface_attachment_status status = get_surface_attachment_status(*node);
        const erhe::scene::Mesh* const support = dynamic_cast<const erhe::scene::Mesh*>(status.target.get());
        if (!is_surface_attached(status.state) || (support == nullptr)) {
            return;
        }
        // Every step carries from the pose before the drag, as the release does.
        Follow_move* known = nullptr;
        for (Follow_move& move : io_moves) {
            if (move.node == node) {
                known = &move;
            }
        }
        if (known != nullptr) {
            node->restore_local_transform(known->before, known->xform_op_stack_before);
            node->update_world_from_node();
        }
        const std::optional<Kit_surface_carry> result = compute_surface_carry(scene_root, *piece, *support, status.attachment);
        if (!result.has_value()) {
            return;
        }
        if (known == nullptr) {
            io_moves.push_back(Follow_move{.node = node, .before = node->parent_from_node_transform(), .after = result.value().local, .xform_op_stack_before = node->copy_xform_op_stack()});
        } else {
            known->after = result.value().local;
        }
        node->set_parent_from_node(result.value().local);
        node->update_world_from_node();
        scene_root.get_scene().update_node_transforms();
    };
    // S6.4: the dragged piece rests again on its support first, then its own attached pieces follow its step.
    carry(std::const_pointer_cast<erhe::scene::Node>(std::dynamic_pointer_cast<const erhe::scene::Node>(dragged.shared_from_this())));
    for (const std::shared_ptr<erhe::Hierarchy>& child : dragged.get_children()) {
        carry(std::dynamic_pointer_cast<erhe::scene::Node>(child));
    }
}

namespace {

// The subtree deepest first without the attached pieces at any depth, which go to out_attached.
void collect_subtree_without_attached(
    const std::shared_ptr<erhe::Hierarchy>&          hierarchy,
    std::vector<std::shared_ptr<erhe::Hierarchy>>&   out_items,
    std::vector<std::shared_ptr<erhe::scene::Node>>& out_attached
)
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : hierarchy->get_children()) {
        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (node && is_surface_attached_to_parent(*node)) {
            out_attached.push_back(node);
            continue;
        }
        collect_subtree_without_attached(child, out_items, out_attached);
    }
    out_items.push_back(hierarchy);
}

} // anonymous namespace

void Level_kits::append_generation_removals(
    Scene_root&                                          scene_root,
    const std::vector<std::shared_ptr<erhe::Hierarchy>>& children,
    const std::vector<bool>&                             kept,
    const std::size_t                                    successor_count,
    std::vector<std::shared_ptr<Operation>>&             out_operations,
    std::vector<Kit_parked_piece>&                       out_parked,
    std::size_t&                                         io_removed_count
)
{
    std::vector<std::shared_ptr<erhe::Hierarchy>>   subtree;
    std::vector<std::shared_ptr<erhe::scene::Node>> attached;
    const std::shared_ptr<erhe::Hierarchy> park = scene_root.get_scene().get_root_node();
    for (std::size_t j = children.size(); j > 0; --j) {
        if ((j - 1 < kept.size()) && kept[j - 1]) {
            continue;
        }
        ++io_removed_count;
        subtree.clear();
        attached.clear();
        collect_subtree_without_attached(children[j - 1], subtree, attached);
        // The attached pieces leave first, so the removal does not take them.
        for (const std::shared_ptr<erhe::scene::Node>& node : attached) {
            if ((j - 1) < successor_count) {
                out_operations.push_back(std::make_shared<Kit_surface_rehome_operation>(node, park));
                out_parked.push_back(Kit_parked_piece{.node = node, .element = j - 1});
                continue;
            }
            const Kit_surface_attachment_status status = get_surface_attachment_status(*node);
            append_surface_detach(node, out_operations);
            if (!m_flow_previewing) {
                log_scene->info(
                    "Kit piece '{}' detached from '{}': the layout's regeneration removed that element",
                    node->get_name(), status.target ? status.target->get_name() : std::string{"(missing)"}
                );
            }
        }
        for (const std::shared_ptr<erhe::Hierarchy>& item : subtree) {
            out_operations.push_back(
                std::make_shared<Item_insert_remove_operation>(
                    Item_insert_remove_operation::Parameters{
                        .context = m_context,
                        .item    = item,
                        .parent  = item->get_parent().lock(),
                        .mode    = Item_insert_remove_operation::Mode::remove
                    }
                )
            );
        }
    }
}

void Level_kits::append_generation_arrivals(
    Scene_root&                                               scene_root,
    const std::span<const Kit_parked_piece>                   parked,
    const std::span<const std::shared_ptr<erhe::scene::Node>> successors,
    std::vector<std::shared_ptr<Operation>>&                  out_operations
)
{
    std::vector<std::shared_ptr<erhe::scene::Node>> supports;
    for (const Kit_parked_piece& piece : parked) {
        const std::shared_ptr<erhe::scene::Node> successor = (piece.element < successors.size()) ? successors[piece.element] : std::shared_ptr<erhe::scene::Node>{};
        if (!successor) {
            continue;
        }
        out_operations.push_back(std::make_shared<Kit_surface_rehome_operation>(piece.node, successor));
        std::optional<Kit_surface_attachment> record = read_kit_surface_attachment(*piece.node);
        if (record.has_value()) {
            record.value().target = successor->get_gltf_uid();
            std::shared_ptr<Operation> write = make_surface_record_operation(piece.node, record);
            if (write) {
                out_operations.push_back(std::move(write));
            }
        }
        if (std::find(supports.begin(), supports.end(), successor) == supports.end()) {
            supports.push_back(successor);
        }
    }
    if (supports.empty()) {
        return;
    }
    // S3.2: carried on the new shapes, as a changed support's pieces are.
    std::shared_ptr<Operation> follow = make_attachment_follow_operation(scene_root, {}, supports);
    if (follow) {
        out_operations.push_back(std::move(follow));
    }
}

} // namespace editor
