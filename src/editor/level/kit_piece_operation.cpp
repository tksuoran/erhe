#include "level/kit_piece_operation.hpp"

#include "level/kit_piece.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "editor_log.hpp"
#include "operations/mesh_primitive_swap.hpp"
#include "scene/node_physics_system.hpp"

#include "erhe_item/item_host.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>

#include <mutex>

namespace editor {

Kit_piece_parameters_operation::Kit_piece_parameters_operation(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    Kit_piece_mesh_state&&                    before,
    Kit_piece_mesh_state&&                    after
)
    : m_mesh  {mesh}
    , m_before{std::move(before)}
    , m_after {std::move(after)}
{
    set_description(fmt::format("[{}] Kit piece parameters {} {}", get_serial(), mesh->get_name(), m_after.parameters));
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
    set_node_collision_shape(*m_mesh, state.collision_shape);
    set_brush_placement(*m_mesh, state.brush, GEO::NO_INDEX, GEO::NO_INDEX);
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

} // namespace editor
