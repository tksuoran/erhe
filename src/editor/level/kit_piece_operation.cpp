#include "level/kit_piece_operation.hpp"

#include "level/kit_piece.hpp"
#include "level/level_kits.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "editor_item_bits.hpp"
#include "editor_log.hpp"
#include "erhe_scene/scene.hpp"
#include "operations/mesh_primitive_swap.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_physics_system.hpp"
#include "scene/scene_root.hpp"

#include "erhe_item/item_host.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>

#include <mutex>

namespace editor {

auto get_piece_motion_mode(const erhe::scene::Mesh& mesh) -> erhe::physics::Motion_mode
{
    const std::optional<Node_physics_data> physics = read_node_physics(mesh);
    return physics.has_value() ? physics.value().motion_mode : erhe::physics::Motion_mode::e_none;
}

auto get_piece_ground(const erhe::scene::Mesh& mesh) -> Kit_piece_ground
{
    return ((mesh.get_flag_bits() & Editor_item_flags::pass_through) != 0) ? Kit_piece_ground::pass_through : Kit_piece_ground::ground;
}

Kit_piece_parameters_operation::Kit_piece_parameters_operation(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    Kit_piece_mesh_state&&                    before,
    Kit_piece_mesh_state&&                    after
)
    : m_mesh  {mesh}
    , m_before{std::move(before)}
    , m_after {std::move(after)}
{
    set_description(
        (m_before.shape == m_after.shape)
            ? fmt::format("[{}] Kit piece parameters {} {}", get_serial(), mesh->get_name(), m_after.parameters)
            : fmt::format("[{}] Kit piece replace {} {} -> {} {}", get_serial(), mesh->get_name(), m_before.shape, m_after.shape, m_after.parameters)
    );
}

Kit_piece_parameters_operation::~Kit_piece_parameters_operation() noexcept = default;

void Kit_piece_parameters_operation::apply(App_context& context, const Kit_piece_mesh_state& state)
{
    erhe::Item_host* const item_host = m_mesh->get_item_host();
    if (item_host == nullptr) {
        set_error("the piece is in no scene");
        log_operations->warn("Op {} failed: {}", describe(), get_error());
        return;
    }
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};
    swap_mesh_primitives(m_mesh, state.primitives);
    // A body goes before its shape is taken (a body without a shape would get a stand-in); a body comes after its
    // shape is given, so it is made with it (as Mesh_operation::restore_physics()).
    const bool body_changes = (get_piece_motion_mode(*m_mesh) != state.motion_mode);
    if (body_changes && (state.motion_mode == erhe::physics::Motion_mode::e_none)) {
        clear_node_physics(*m_mesh);
    }
    set_node_collision_shape(*m_mesh, state.collision_shape);
    if (body_changes && (state.motion_mode != erhe::physics::Motion_mode::e_none)) {
        m_mesh->set_value(Node_physics::motion_mode_property, state.motion_mode);
    }
    if (state.ground == Kit_piece_ground::pass_through) {
        m_mesh->enable_flag_bits(Editor_item_flags::pass_through);
    } else {
        m_mesh->disable_flag_bits(Editor_item_flags::pass_through);
    }
    set_brush_placement(*m_mesh, state.brush, GEO::NO_INDEX, GEO::NO_INDEX);
    m_mesh->set_value(Kit_piece::shape_property,      state.shape);
    m_mesh->set_value(Kit_piece::parameters_property, state.parameters);
    context.app_message_bus->mesh_geometry_changed.send_message(Mesh_geometry_changed_message{.mesh = m_mesh});
}

void Kit_piece_parameters_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    apply(context, m_after);
}

void Kit_piece_parameters_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    apply(context, m_before);
}

void Kit_piece_parameters_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    out_items.insert(m_mesh.get());
}

Kit_surface_rehome_operation::Kit_surface_rehome_operation(const std::shared_ptr<erhe::scene::Node>& node, const std::shared_ptr<erhe::Hierarchy>& parent)
    : m_node        {node}
    , m_parent_after{parent}
{
    set_description(fmt::format("[{}] Kit surface attachment re-homed: {} under {}", get_serial(), node->get_name(), parent->get_name()));
}

void Kit_surface_rehome_operation::execute(App_context& context)
{
    static_cast<void>(context);
    if (!m_executed) {
        m_parent_before = m_node->get_parent().lock();
        m_index_before  = m_node->get_index_in_parent();
        m_executed      = true;
    }
    // The base class set_parent keeps the local transform; the world transform is written again from it.
    const erhe::scene::Trs_transform local = m_node->parent_from_node_transform();
    m_node->erhe::Hierarchy::set_parent(m_parent_after, m_parent_after->get_child_count());
    m_node->set_parent_from_node(local);
}

void Kit_surface_rehome_operation::undo(App_context& context)
{
    static_cast<void>(context);
    const erhe::scene::Trs_transform local = m_node->parent_from_node_transform();
    m_node->erhe::Hierarchy::set_parent(m_parent_before, m_index_before);
    m_node->set_parent_from_node(local);
}

void Kit_surface_rehome_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    out_items.insert(m_node.get());
}

Kit_attachment_follow_operation::Kit_attachment_follow_operation(
    std::vector<std::shared_ptr<erhe::scene::Node>>&& moved_nodes,
    std::vector<std::shared_ptr<erhe::scene::Node>>&& changed_pieces,
    std::optional<Kit_snap_pair>&&                    snap,
    std::vector<std::shared_ptr<erhe::scene::Node>>&& requesting,
    std::vector<Kit_new_connection>&&                 new_connections,
    const std::optional<Kit_flow_overrides>&          flow_overrides,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& auto_connect_excluded,
    std::vector<Kit_surface_snap>&&                   surface_snaps
)
    : m_moved_nodes    {std::move(moved_nodes)}
    , m_changed_pieces {std::move(changed_pieces)}
    , m_snap           {std::move(snap)}
    , m_requesting     {std::move(requesting)}
    , m_new_connections{std::move(new_connections)}
    , m_flow_overrides {flow_overrides}
    , m_auto_connect_excluded{auto_connect_excluded}
    , m_surface_snaps  {std::move(surface_snaps)}
{
    set_description(fmt::format("[{}] Kit attachments follow", get_serial()));
}

Kit_attachment_follow_operation::~Kit_attachment_follow_operation() noexcept = default;

void Kit_attachment_follow_operation::execute(App_context& context)
{
    if (m_recorded) {
        for (const std::shared_ptr<Operation>& operation : m_operations) {
            operation->execute(context);
        }
        return;
    }
    m_recorded = true;
    Scene_root* scene_root = nullptr;
    for (const std::vector<std::shared_ptr<erhe::scene::Node>>* nodes : {&m_moved_nodes, &m_changed_pieces, &m_requesting}) {
        for (const std::shared_ptr<erhe::scene::Node>& node : *nodes) {
            if ((scene_root == nullptr) && node) {
                scene_root = dynamic_cast<Scene_root*>(node->get_item_host());
            }
        }
    }
    for (const Kit_surface_snap& snap : m_surface_snaps) {
        if ((scene_root == nullptr) && snap.node) {
            scene_root = dynamic_cast<Scene_root*>(snap.node->get_item_host());
        }
    }
    if ((scene_root == nullptr) && m_snap.has_value() && m_snap.value().piece) {
        scene_root = dynamic_cast<Scene_root*>(m_snap.value().piece->get_item_host());
    }
    if ((scene_root == nullptr) || (context.level_kits == nullptr)) {
        return;
    }
    Level_kits& level_kits = *context.level_kits;
    // C6.2: the overrides of the call that made the entry, which may run after the call (a held stack defers it).
    const std::optional<Kit_flow_overrides> outer = level_kits.get_flow_overrides();
    level_kits.set_flow_overrides(m_flow_overrides);
    const std::string refusal = record(context, *scene_root);
    level_kits.set_flow_overrides(outer);
    level_kits.clear_fitted_pairs();
    if (!refusal.empty()) {
        // K0.3: what ran was undone; the report of a refused entry states the refusal only.
        m_operations.clear();
        Kit_flow_report* const report = level_kits.get_flow_report();
        if (report != nullptr) {
            report->clear();
            report->refusal = refusal;
        }
        set_refused(refusal);
    }
    level_kits.update_flow_summary();
}

auto Kit_attachment_follow_operation::record(App_context& context, Scene_root& scene_root) -> std::string
{
    Level_kits& level_kits = *context.level_kits;
    // Runs the operations from begin; at a refusal everything this operation ran is undone, in reverse.
    const auto run = [this, &context](const std::size_t begin) -> std::string {
        const Operation* const refused = execute_unless_refused(context, std::span<const std::shared_ptr<Operation>>{m_operations}.subspan(begin));
        if (refused == nullptr) {
            return {};
        }
        for (std::size_t i = begin; i > 0; --i) {
            m_operations[i - 1]->undo(context);
        }
        return refused->get_error();
    };
    // C3.13: the entry's report (an empty one says a flow ran and changed nothing).
    if (level_kits.get_flow_report() != nullptr) {
        for (const Kit_new_connection& connection : m_new_connections) {
            level_kits.report_new_connection(connection);
        }
    }
    // C3.15: floors fit the moved walls standing on them first (each edit's own follow-ups make the walls follow).
    // K0.3: an outline a floor refuses refuses the change; nothing of this operation ran yet.
    // The fits, the return to the edge and the detach pass read world frames: the moves of this change reach every node
    // below the moved ones first (a move writes the moved node's world matrix alone).
    scene_root.get_scene().update_node_transforms();
    std::string refusal = level_kits.build_floor_fits(scene_root, m_moved_nodes, m_operations);
    if (refusal.empty()) {
        refusal = run(0);
    }
    if (!refusal.empty()) {
        return refusal;
    }
    // C3.16: then the walls joined end to end to the moved walls (no floor under them) fit.
    const std::size_t wall_begin = m_operations.size();
    // A wall's partner moved, or changed (its connector moved with its length): both make it fit.
    std::vector<std::shared_ptr<erhe::scene::Node>> fit_movers = m_moved_nodes;
    fit_movers.insert(fit_movers.end(), m_changed_pieces.begin(), m_changed_pieces.end());
    level_kits.build_wall_fits(scene_root, fit_movers, m_operations);
    refusal = run(wall_begin);
    if (!refusal.empty()) {
        return refusal;
    }
    // C3: the value flow, applied, so the following joins the pieces at their new connectors.
    const std::size_t flow_begin = m_operations.size();
    std::vector<std::shared_ptr<erhe::scene::Node>> flowed;
    level_kits.build_connection_flow(scene_root, m_changed_pieces, m_requesting, m_snap.has_value() ? &m_snap.value() : nullptr, m_operations, flowed);
    refusal = run(flow_begin);
    if (!refusal.empty()) {
        return refusal;
    }
    const std::size_t follow_begin = m_operations.size();
    std::vector<std::shared_ptr<erhe::scene::Node>> changed = m_changed_pieces;
    changed.insert(changed.end(), flowed.begin(), flowed.end());
    // The fitted walls moved and changed: the pieces attached to them follow.
    const std::vector<std::shared_ptr<erhe::scene::Node>>& fitted = level_kits.get_fitted_walls();
    changed.insert(changed.end(), fitted.begin(), fitted.end());
    level_kits.build_attachment_follow_ups(scene_root, m_moved_nodes, changed, m_snap.has_value() ? &m_snap.value() : nullptr, m_operations);
    refusal = run(follow_begin);
    if (!refusal.empty()) {
        return refusal;
    }
    level_kits.clear_fitted_pairs();
    // C5: then the changed pieces connect where their connectors sit on free compatible ones, and the new
    // connections' pieces take their values (one round; design K7.3).
    std::vector<std::shared_ptr<erhe::scene::Node>> pieces = changed;
    pieces.insert(pieces.end(), m_moved_nodes.begin(), m_moved_nodes.end());
    pieces.insert(pieces.end(), m_requesting.begin(), m_requesting.end());
    if (m_snap.has_value() && m_snap.value().piece) {
        pieces.push_back(m_snap.value().piece);
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> owners;
    const std::size_t connect_begin = m_operations.size();
    level_kits.build_auto_connections(scene_root, pieces, m_auto_connect_excluded, m_operations, owners);
    refusal = run(connect_begin);
    if (!refusal.empty()) {
        return refusal;
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> reflowed;
    if (!owners.empty()) {
        const std::size_t reflow_begin = m_operations.size();
        // C3.6: the author's edit of this change is kept in the reflow too.
        level_kits.build_connection_flow(scene_root, m_changed_pieces, owners, nullptr, m_operations, reflowed);
        refusal = run(reflow_begin);
        if (!refusal.empty()) {
            return refusal;
        }
    }
    // The surface pass, after the connections and the flow settled (S4.5): first the supports whose shape changed carry
    // their attached pieces (a preview step moves them without writing records, S3.7), then the moved and snapped nodes
    // are judged (a preview step attaches and detaches nothing).
    changed.insert(changed.end(), reflowed.begin(), reflowed.end());
    std::vector<std::shared_ptr<erhe::scene::Node>> judged_elsewhere = m_moved_nodes;
    for (const Kit_surface_snap& snap : m_surface_snaps) {
        judged_elsewhere.push_back(snap.node);
    }
    scene_root.get_scene().update_node_transforms();
    std::vector<const erhe::scene::Node*> carried;
    level_kits.build_surface_carry_follow_ups(
        scene_root, changed, judged_elsewhere,
        level_kits.is_flow_previewing() ? Kit_surface_carry_record::skip : Kit_surface_carry_record::write,
        m_operations, carried
    );
    if (level_kits.is_flow_previewing()) {
        return {};
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> connected = m_requesting;
    for (const Kit_new_connection& connection : m_new_connections) {
        connected.push_back(connection.node);
        connected.push_back(connection.target);
    }
    connected.insert(connected.end(), owners.begin(), owners.end());
    if (m_snap.has_value()) {
        connected.push_back(m_snap.value().piece);
        connected.push_back(m_snap.value().target);
    }
    std::erase(connected, std::shared_ptr<erhe::scene::Node>{});
    scene_root.get_scene().update_node_transforms();
    const std::size_t surface_begin = m_operations.size();
    level_kits.build_surface_attachment_follow_ups(scene_root, m_moved_nodes, m_surface_snaps, connected, m_operations);
    return run(surface_begin);
}

void Kit_attachment_follow_operation::undo(App_context& context)
{
    for (auto i = m_operations.rbegin(), end = m_operations.rend(); i != end; ++i) {
        (*i)->undo(context);
    }
}

void Kit_attachment_follow_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    for (const std::shared_ptr<Operation>& operation : m_operations) {
        operation->collect_item_references(out_items);
    }
}

} // namespace editor
