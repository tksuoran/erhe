#include "level/kit_piece_operation.hpp"

#include "level/kit_piece.hpp"
#include "level/level_kits.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "editor_item_bits.hpp"
#include "editor_log.hpp"
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

Kit_attachment_follow_operation::Kit_attachment_follow_operation(
    std::vector<std::shared_ptr<erhe::scene::Node>>&& moved_nodes,
    std::vector<std::shared_ptr<erhe::scene::Node>>&& changed_pieces,
    std::optional<Kit_snap_pair>&&                    snap
)
    : m_moved_nodes   {std::move(moved_nodes)}
    , m_changed_pieces{std::move(changed_pieces)}
    , m_snap          {std::move(snap)}
{
    set_description(fmt::format("[{}] Kit attachments follow", get_serial()));
}

Kit_attachment_follow_operation::~Kit_attachment_follow_operation() noexcept = default;

void Kit_attachment_follow_operation::execute(App_context& context)
{
    if (!m_recorded) {
        m_recorded = true;
        Scene_root* scene_root = nullptr;
        for (const std::vector<std::shared_ptr<erhe::scene::Node>>* nodes : {&m_moved_nodes, &m_changed_pieces}) {
            for (const std::shared_ptr<erhe::scene::Node>& node : *nodes) {
                if ((scene_root == nullptr) && node) {
                    scene_root = dynamic_cast<Scene_root*>(node->get_item_host());
                }
            }
        }
        if ((scene_root == nullptr) && m_snap.has_value() && m_snap.value().piece) {
            scene_root = dynamic_cast<Scene_root*>(m_snap.value().piece->get_item_host());
        }
        if ((scene_root != nullptr) && (context.level_kits != nullptr)) {
            context.level_kits->build_attachment_follow_ups(*scene_root, m_moved_nodes, m_changed_pieces, m_snap.has_value() ? &m_snap.value() : nullptr, m_operations);
        }
    }
    for (const std::shared_ptr<Operation>& operation : m_operations) {
        operation->execute(context);
    }
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
