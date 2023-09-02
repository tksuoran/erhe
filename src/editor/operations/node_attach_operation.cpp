#include "operations/node_attach_operation.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_log/log_glm.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtx/matrix_decompose.hpp>

#include <sstream>

namespace editor
{

auto Node_attach_operation::describe() const -> std::string
{
    return fmt::format(
        "Node_attach_operation(attachment = {} {}, host node before = {}, host node after = {})",
        m_attachment->get_type_name(),
        m_attachment->get_name(),
        m_host_node_before ? m_host_node_before->get_name() : "(empty)",
        m_host_node_after ? m_host_node_after->get_name() : "(empty)"
    );
}

Node_attach_operation::Node_attach_operation() = default;

Node_attach_operation::Node_attach_operation(
    const std::shared_ptr<erhe::scene::Node_attachment>& attachment,
    const std::shared_ptr<erhe::scene::Node>&            host_node
)
    : m_attachment     {attachment}
    , m_host_node_after{host_node}
{
}

void Node_attach_operation::execute(Editor_context& context)
{
    log_operations->trace("Op Execute {}", describe());

    auto* node = m_attachment->get_node();
    m_host_node_before =
        (node != nullptr)
        ? std::static_pointer_cast<erhe::scene::Node>(node->shared_from_this())
        : std::shared_ptr<erhe::scene::Node>{};
    if (m_host_node_before) {
        m_host_node_before->detach(m_attachment.get());
    }

    if (m_host_node_after) {
        m_host_node_after->attach(m_attachment);
    }

    context.selection->sanity_check();
}

void Node_attach_operation::undo(Editor_context& context)
{
    log_operations->trace("Op Undo {}", describe());

    auto* node = m_attachment->get_node();
    m_host_node_after =
        (node != nullptr)
        ? std::static_pointer_cast<erhe::scene::Node>(node->shared_from_this())
        : std::shared_ptr<erhe::scene::Node>{};
    if (m_host_node_after) {
        m_host_node_after->detach(m_attachment.get());
    }

    if (m_host_node_before) {
        m_host_node_before->attach(m_attachment);
    }

    context.selection->sanity_check();
}

}
