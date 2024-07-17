#include "operations/item_reposition_in_parent_operation.hpp"

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
        "[{}] Item_reposition_in_parent_operation(child = {}, place_before = {}, place_after = {})",
        get_serial(),
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
