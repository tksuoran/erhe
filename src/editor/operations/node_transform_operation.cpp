#include "operations/node_transform_operation.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "app_message_bus.hpp"
#include "scene/node_physics_system.hpp"
#include "time.hpp"

#include "erhe_log/log_glm.hpp"

//#include <sstream>

namespace editor {

//auto Node_operation::describe() const -> std::string
//{
//    std::stringstream ss;
//    bool first = true;
//    for (const auto& entry : m_entries) {
//        if (first) {
//            first = false;
//        } else {
//            ss << ", ";
//        }
//        ss << entry.node->get_name();
//        using erhe::scene::Node_data;
//        const auto changed = Node_data::diff_mask(entry.before, entry.after);
//        if (changed & Node_data::bit_transform) ss << " transform";
//    }
//    return ss.str();
//}
//
//void Node_operation::execute(App_context&)
//{
//    log_operations->trace("Op Execute {}", describe());
//
//    for (auto& entry : m_entries) {
//        entry.node->node_data = entry.after;
//    }
//}
//
//void Node_operation::undo(App_context&)
//{
//    log_operations->trace("Op Undo {}", describe());
//
//    for (const auto& entry : m_entries) {
//        entry.node->node_data = entry.before;
//    }
//}
//
//void Node_operation::add_entry(Entry&& entry)
//{
//    m_entries.emplace_back(entry);
//}

// ----------------------------------------------------------------------------

Node_transform_operation::Node_transform_operation(const Parameters& parameters)
    : m_parameters{parameters}
{
    set_description(
        fmt::format(
            "[{}] Trs_transform {} translate before = {}, translate after = {}",
            get_serial(),
            m_parameters.node->get_name(),
            m_parameters.parent_from_node_before.get_translation(),
            m_parameters.parent_from_node_after.get_translation()
        )
    );
}

void Node_transform_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    if (m_parameters.time_duration == 0.0f) {
        if (m_xform_op_stack_after_recorded) {
            // Redo: restore the recorded transform and stack verbatim.
            m_parameters.node->restore_local_transform(m_parameters.parent_from_node_after, m_xform_op_stack_after);
        } else if (m_parameters.first_execute == Node_transform_first_execute::record_only) {
            // A physics-driven drag: the simulation placed the node and its
            // body is moving; record without writing or snapping.
            m_xform_op_stack_after          = m_parameters.xform_op_stack_after;
            m_xform_op_stack_after_recorded = true;
            return;
        } else {
            m_parameters.node->set_parent_from_node(m_parameters.parent_from_node_after);
            m_xform_op_stack_after          = m_parameters.node->copy_xform_op_stack();
            m_xform_op_stack_after_recorded = true;
        }
        context.app_message_bus->node_touched.send_message(
            Node_touched_message{
                .source = Node_touch_source::operation_stack,
                .node   = m_parameters.node.get()
            }
        );
        // Snap the node's rigid body to the new pose at rest so the simulation does
        // not react to this discrete (non-interactive) move with a kinematic velocity
        // injection or corrective impulse.
        Node_physics_system* const system = find_node_physics_system(*m_parameters.node.get());
        if (system != nullptr) {
            system->teleport_to_node(*m_parameters.node.get());
        }
    } else {
        context.time->begin_transform_animation(
            m_parameters.node,
            m_parameters.parent_from_node_before,
            m_parameters.parent_from_node_after,
            m_parameters.time_duration
        );
    }
}

void Node_transform_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    m_parameters.node->restore_local_transform(m_parameters.parent_from_node_before, m_parameters.xform_op_stack_before);
    context.app_message_bus->node_touched.send_message(
        Node_touched_message{
            .source = Node_touch_source::operation_stack,
            .node   = m_parameters.node.get()
        }
    );
    // Snap the node's rigid body to the restored pose at rest (see execute()).
    Node_physics_system* const system = find_node_physics_system(*m_parameters.node.get());
    if (system != nullptr) {
        system->teleport_to_node(*m_parameters.node.get());
    }
}

}
