#include "operations/node_transform_operation.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_log/log_glm.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtx/matrix_decompose.hpp>

#include <sstream>

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
//void Node_operation::execute(Editor_context&)
//{
//    log_operations->trace("Op Execute {}", describe());
//
//    for (auto& entry : m_entries) {
//        entry.node->node_data = entry.after;
//    }
//}
//
//void Node_operation::undo(Editor_context&)
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
}

auto Node_transform_operation::describe() const -> std::string
{
    glm::vec3 scale_before;
    glm::quat orientation_before;
    glm::vec3 translation_before;
    glm::vec3 skew_before;
    glm::vec4 perspective_before;

    glm::vec3 scale_after;
    glm::quat orientation_after;
    glm::vec3 translation_after;
    glm::vec3 skew_after;
    glm::vec4 perspective_after;
    glm::decompose(
        m_parameters.parent_from_node_before.get_matrix(),
        scale_before,
        orientation_before,
        translation_before,
        skew_before,
        perspective_before
    );
    glm::decompose(
        m_parameters.parent_from_node_after.get_matrix(),
        scale_after,
        orientation_after,
        translation_after,
        skew_after,
        perspective_after
    );
    return fmt::format(
        "[{}] Trs_transform {} translate before = {}, translate after = {}",
        get_serial(),
        m_parameters.node->get_name(),
        translation_before,
        translation_after
    );
}

void Node_transform_operation::execute(Editor_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    m_parameters.node->set_parent_from_node(m_parameters.parent_from_node_after);
    context.editor_message_bus->send_message(
        Editor_message{
            .update_flags = Message_flag_bit::c_flag_bit_node_touched_operation_stack,
            .node         = m_parameters.node.get()
        }
    );
}

void Node_transform_operation::undo(Editor_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    m_parameters.node->set_parent_from_node(m_parameters.parent_from_node_before);
    context.editor_message_bus->send_message(
        Editor_message{
            .update_flags = Message_flag_bit::c_flag_bit_node_touched_operation_stack,
            .node         = m_parameters.node.get()
        }
    );
}

}
