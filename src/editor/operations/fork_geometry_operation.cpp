#include "operations/fork_geometry_operation.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "operations/mesh_primitive_swap.hpp"

#include "erhe_item/item_host.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <vector>

namespace editor {

Fork_geometry_operation::Fork_geometry_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    set_description(m_parameters.description.empty() ? "Fork geometry" : m_parameters.description);
}

void Fork_geometry_operation::execute(App_context& context)
{
    apply(context, m_parameters.after);
}

void Fork_geometry_operation::undo(App_context& context)
{
    apply(context, m_parameters.before);
}

void Fork_geometry_operation::apply(App_context& context, const erhe::scene::Mesh_primitive& mesh_primitive)
{
    if (!m_parameters.mesh) {
        set_error("Fork_geometry_operation: mesh is null");
        return;
    }
    erhe::scene::Node* node = m_parameters.mesh.get();
    if (node == nullptr) {
        set_error("Fork_geometry_operation: mesh node is null");
        return;
    }
    erhe::Item_host* item_host = node->get_item_host();
    if (item_host == nullptr) {
        set_error("Fork_geometry_operation: item host is null");
        return;
    }
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    std::vector<erhe::scene::Mesh_primitive> new_primitives = m_parameters.mesh->get_primitives();
    if (m_parameters.primitive_index >= new_primitives.size()) {
        set_error("Fork_geometry_operation: primitive index out of range");
        return;
    }
    new_primitives[m_parameters.primitive_index] = mesh_primitive;

    // No physics rebuild: forking is a position-identical copy, so the convex
    // hull is unchanged and the existing Node_physics stays valid.
    swap_mesh_primitives(m_parameters.mesh, new_primitives);

    // Announce the geometry change (eager prune for the component-selection store;
    // its entries for both the shared and forked Geometry are re-evaluated lazily
    // by is_live()).
    context.app_message_bus->mesh_geometry_changed.send_message(
        Mesh_geometry_changed_message{.mesh = m_parameters.mesh}
    );
}

}
