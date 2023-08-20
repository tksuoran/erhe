#include "operations/node_operation.hpp"

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

auto Node_operation::describe() const -> std::string
{
    std::stringstream ss;
    bool first = true;
    for (const auto& entry : m_entries) {
        if (first) {
            first = false;
        } else {
            ss << ", ";
        }
        ss << entry.node->get_name();
        using erhe::scene::Node_data;
        const auto changed = Node_data::diff_mask(entry.before, entry.after);
        if (changed & Node_data::bit_transform) ss << " transform";
    }
    return ss.str();
}

void Node_operation::execute(Editor_context&)
{
    log_operations->trace("Op Execute {}", describe());

    for (auto& entry : m_entries) {
        entry.node->node_data = entry.after;
    }
}

void Node_operation::undo(Editor_context&)
{
    log_operations->trace("Op Undo {}", describe());

    for (const auto& entry : m_entries) {
        entry.node->node_data = entry.before;
    }
}

void Node_operation::add_entry(Entry&& entry)
{
    m_entries.emplace_back(entry);
}

// ----------------------------------------------------------------------------

Node_transform_operation::Node_transform_operation(
    const Parameters& parameters
)
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
        "Trs_transform {} translate before = {}, translate after = {}",
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

// ----------------------------------------------------------------------------

auto Attach_operation::describe() const -> std::string
{
    return fmt::format(
        "Attach_operation(attachment = {} {}, host node before = {}, host node after = {})",
        m_attachment->get_type_name(),
        m_attachment->get_name(),
        m_host_node_before ? m_host_node_before->get_name() : "(empty)",
        m_host_node_after ? m_host_node_after->get_name() : "(empty)"
    );
}

Attach_operation::Attach_operation() = default;

Attach_operation::Attach_operation(
    const std::shared_ptr<erhe::scene::Node_attachment>& attachment,
    const std::shared_ptr<erhe::scene::Node>&            host_node
)
    : m_attachment     {attachment}
    , m_host_node_after{host_node}
{
}

void Attach_operation::execute(Editor_context& context)
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

void Attach_operation::undo(Editor_context& context)
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

// ----------------------------------------------------------------------------

auto Item_parent_change_operation::describe() const -> std::string
{
    return fmt::format(
        "Item_parent_change_operation(child_node = {}, parent before = {}, parent after = {})",
        m_child->get_name(),
        m_parent_before ? m_parent_before->get_name() : "(empty)",
        m_parent_after  ? m_parent_after ->get_name() : "(empty)"
    );
}

Item_parent_change_operation::Item_parent_change_operation() = default;

Item_parent_change_operation::Item_parent_change_operation(
    const std::shared_ptr<erhe::Hierarchy>& parent,
    const std::shared_ptr<erhe::Hierarchy>& child,
    const std::shared_ptr<erhe::Hierarchy>  place_before,
    const std::shared_ptr<erhe::Hierarchy>  place_after
)
    : m_child        {child}
    , m_parent_before{child->get_parent()}
    , m_parent_after {parent}
    , m_place_before {place_before}
    , m_place_after  {place_after}
{
    // Only at most one place can be set
    ERHE_VERIFY(!place_before || !place_after);
}

void Item_parent_change_operation::execute(Editor_context& context)
{
    log_operations->trace("Op Execute {}", describe());

    ERHE_VERIFY(m_child->get_parent().lock() == m_parent_before);

    m_parent_before_index = m_child->get_index_in_parent();
    if (m_parent_after) {
        m_parent_after_index = m_place_before
            ? m_place_before->get_index_in_parent()
            : m_place_after
                ? m_place_after->get_index_in_parent() + 1
                : m_parent_after->get_child_count();

        m_child->set_parent(m_parent_after, m_parent_after_index);
    } else {
        m_child->set_parent({});
    }

    context.selection->sanity_check();
}

void Item_parent_change_operation::undo(Editor_context& context)
{
    log_operations->trace("Op Undo {}", describe());

    ERHE_VERIFY(m_child->get_parent().lock() == m_parent_after);

    if (m_parent_before) {
        m_child->set_parent(m_parent_before, m_parent_before_index);
    } else if (m_parent_after) {
        m_child->set_parent({});
    }

    context.selection->sanity_check();
}

// ----------------------------------------------------------------------------

Item_reposition_in_parent_operation::Item_reposition_in_parent_operation() = default;

Item_reposition_in_parent_operation::Item_reposition_in_parent_operation(
    const std::shared_ptr<erhe::Hierarchy>& child_node,
    const std::shared_ptr<erhe::Hierarchy>  place_before,
    const std::shared_ptr<erhe::Hierarchy>  place_after
)
    : m_child       {child_node  }
    , m_place_before{place_before}
    , m_place_after {place_after }
{
    // Exactly one of before_node and after_node must be set
    ERHE_VERIFY(static_cast<bool>(place_before) != static_cast<bool>(place_after));
}

auto Item_reposition_in_parent_operation::describe() const -> std::string
{
    return fmt::format(
        "Item_reposition_in_parent_operation(child = {}, place_before = {}, place_after = {})",
        m_child->get_name(),
        m_place_before ? m_place_before->get_name() : "()",
        m_place_after  ? m_place_after ->get_name() : "()"
    );
}

void Item_reposition_in_parent_operation::execute(Editor_context& context)
{
    log_operations->trace("Op Execute {}", describe());

    auto parent = m_child->get_parent().lock();
    ERHE_VERIFY(parent);

    auto& parent_children = parent->get_mutable_children();

    m_before_index = m_child->get_index_in_parent();

    ERHE_VERIFY(m_before_index < parent_children.size());
    ERHE_VERIFY(parent_children[m_before_index] == m_child);
    parent_children.erase(parent_children.begin() + m_before_index);

    const std::size_t after_index = m_place_before
        ? m_place_before->get_index_in_parent()
        : m_place_after ->get_index_in_parent() + 1;
    ERHE_VERIFY(after_index <= parent_children.size());

    parent_children.insert(parent_children.begin() + after_index, m_child);

    context.selection->sanity_check();
}

void Item_reposition_in_parent_operation::undo(Editor_context& context)
{
    log_operations->trace("Op Undo {}", describe());

    auto parent = m_child->get_parent().lock();
    ERHE_VERIFY(parent);

    auto& parent_children = parent->get_mutable_children();

    const std::size_t after_index = m_child->get_index_in_parent();

    ERHE_VERIFY(m_before_index <= parent_children.size());
    ERHE_VERIFY(after_index <= parent_children.size());
    ERHE_VERIFY(parent_children[after_index] == m_child);
    parent_children.erase(parent_children.begin() + after_index);
    parent_children.insert(parent_children.begin() + m_before_index, m_child);

    context.selection->sanity_check();
}


} // namespace editor
