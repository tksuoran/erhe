#include "operations/flip_joint_operation.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "editor_log.hpp"
#include "scene/node_joint.hpp"

#include "erhe_scene/node.hpp"

namespace editor {

Flip_joint_operation::Flip_joint_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    set_description(
        fmt::format(
            "[{}] Flip joint {}",
            get_serial(),
            m_parameters.moved_node ? m_parameters.moved_node->get_name() : std::string{}
        )
    );
}

void Flip_joint_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());

    // Apply both transforms first, then rebuild so the constraint re-captures the
    // (now coincident) joint frames from the new node poses.
    m_parameters.moved_node->set_parent_from_node(m_parameters.moved_after);
    m_parameters.frame_node->set_parent_from_node(m_parameters.frame_after);
    if (m_parameters.node_joint) {
        m_parameters.node_joint->rebuild();
    }

    context.app_message_bus->node_touched.send_message(
        Node_touched_message{
            .source = Node_touch_source::operation_stack,
            .node   = m_parameters.moved_node.get()
        }
    );
}

void Flip_joint_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());

    // Restore both transforms first, then rebuild so the constraint re-captures the
    // original joint frames (rebuild must run after the transforms in this direction
    // too - see the class comment).
    m_parameters.moved_node->set_parent_from_node(m_parameters.moved_before);
    m_parameters.frame_node->set_parent_from_node(m_parameters.frame_before);
    if (m_parameters.node_joint) {
        m_parameters.node_joint->rebuild();
    }

    context.app_message_bus->node_touched.send_message(
        Node_touched_message{
            .source = Node_touch_source::operation_stack,
            .node   = m_parameters.moved_node.get()
        }
    );
}

}
