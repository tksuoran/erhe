#include "operations/item_parent_change_operation.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

Item_parent_change_operation::Item_parent_change_operation() = default;

Item_parent_change_operation::Item_parent_change_operation(
    const std::shared_ptr<erhe::Hierarchy>& parent,
    const std::shared_ptr<erhe::Hierarchy>& child,
    const std::shared_ptr<erhe::Hierarchy>& place_before,
    const std::shared_ptr<erhe::Hierarchy>& place_after
)
    : m_child        {child}
    , m_parent_before{child->get_parent()}
    , m_parent_after {parent}
    , m_place_before {place_before}
    , m_place_after  {place_after}
{
    // Only at most one place can be set
    ERHE_VERIFY(!place_before || !place_after);

    set_description(
        fmt::format(
            "[{}] Item_parent_change_operation(child_node = {}, parent before = {}, parent after = {})",
            get_serial(),
            m_child->get_name(),
            m_parent_before ? m_parent_before->get_name() : "(empty)",
            m_parent_after  ? m_parent_after ->get_name() : "(empty)"
        )
    );
}

void Item_parent_change_operation::execute(App_context& context)
{
    static_cast<void>(context);
    log_operations->trace("Op Execute {}", describe());

    ERHE_VERIFY(m_child->get_parent().lock() == m_parent_before);

    m_parent_before_index = m_child->get_index_in_parent();
    const std::shared_ptr<erhe::scene::Xformable> xformable = std::dynamic_pointer_cast<erhe::scene::Xformable>(m_child);
    if (xformable) {
        m_local_before = xformable->authored_parent_from_node_transform();
    }
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

#if !defined(NDEBUG)
    context.selection->sanity_check();
#endif
}

void Item_parent_change_operation::undo(App_context& context)
{
    static_cast<void>(context);
    log_operations->trace("Op Undo {}", describe());

    ERHE_VERIFY(m_child->get_parent().lock() == m_parent_after);

    if (m_parent_before) {
        // Keeps the local transform; Xformable::set_parent would re-derive it through the parent inverse.
        m_child->erhe::Hierarchy::set_parent(m_parent_before, m_parent_before_index);
        const std::shared_ptr<erhe::scene::Xformable> xformable = std::dynamic_pointer_cast<erhe::scene::Xformable>(m_child);
        if (xformable && m_local_before.has_value()) {
            xformable->set_parent_from_node(m_local_before.value());
        }
    } else if (m_parent_after) {
        m_child->set_parent({});
    }

#if !defined(NDEBUG)
    context.selection->sanity_check();
#endif
}

}
