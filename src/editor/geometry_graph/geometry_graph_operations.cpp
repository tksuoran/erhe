#include "geometry_graph/geometry_graph_operations.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"

namespace editor {

void Geometry_graph_display_designation_operation::apply(App_context& context, const std::size_t display_id, const std::size_t ghost_id)
{
    m_graph_mesh->graph().set_display_node_id(display_id);
    m_graph_mesh->graph().set_ghost_node_id(ghost_id);
    if (context.app_message_bus != nullptr) {
        context.app_message_bus->graph_mesh_binding_changed.queue_message(Graph_mesh_binding_changed_message{});
    }
}

} // namespace editor
