#include "operations/item_insert_remove_operation.hpp"
#include "operations/item_parent_change_operation.hpp"
#include "erhe_item/item_host.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "tools/selection_tool.hpp"

#include <sstream>

namespace editor {

auto c_str(Item_insert_remove_operation::Mode mode) -> const char*
{
    switch (mode) {
        //using enum Mode;
        case Item_insert_remove_operation::Mode::insert: return "Item_insert";
        case Item_insert_remove_operation::Mode::remove: return "Item_remove";
        default: return "?";
    }
}

auto Item_insert_remove_operation::describe() const -> std::string
{
    ERHE_VERIFY(m_item);
    bool before_parent = m_before_parent.operator bool();
    bool after_parent  = m_after_parent.operator bool();
    const erhe::Hierarchy* parent = before_parent ? m_before_parent.get() : m_after_parent.get();
    std::stringstream ss;
    bool first = true;
    for (const std::shared_ptr<Item_parent_change_operation>& op : m_parent_changes) {
        if (!first) {
            ss << ", ";
        }
        ss << op->describe();
    }

    return fmt::format(
        "[{}] {} {}, {}{}, {} parent changes: {}",
        get_serial(),
        c_str(m_mode),
        m_item->get_name(),
        before_parent ? "before parent = " : after_parent ? "after parent = " : "no parent",
        (parent != nullptr) ? parent->get_name() : "",
        m_parent_changes.size(),
        ss.str()
    );
}

Item_insert_remove_operation::Item_insert_remove_operation(const Parameters& parameters)
    : m_mode{parameters.mode}
{
    auto& selection = *parameters.context.selection;
    m_item                   = parameters.item,
    m_selection_before       = selection.get_selection();
    m_index_in_parent_insert = parameters.index_in_parent;

    if (parameters.mode == Mode::insert) {
        m_selection_after = selection.get_selection();
        m_after_parent    = parameters.parent;
    }

    if (parameters.mode == Mode::remove) {
        m_after_parent = std::shared_ptr<erhe::scene::Node>{};

        const auto& children = parameters.item->get_children();
        const auto parent = parameters.item->get_parent().lock();
        for (const auto& child : children) {
            m_parent_changes.push_back(
                std::make_shared<Item_parent_change_operation>(
                    parent,
                    child,
                    std::shared_ptr<erhe::Hierarchy>{},
                    std::shared_ptr<erhe::Hierarchy>{}
                )
            );
        }

        log_operations->trace("selection size = {}", m_selection_after.size());
    }
}

void Item_insert_remove_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());

    erhe::Item_host_lock_guard scene_lock{m_item.get()};

    if (m_mode == Mode::remove) {
        m_before_parent = m_item->get_parent().lock();
        m_index_in_parent = m_item->get_index_in_parent();

        const auto& children = m_item->get_children();
        m_parent_changes.clear();
        for (const auto& child : children) {
            log_operations->trace("  child -> parent {}", child->get_name(), m_before_parent->get_name());
            m_parent_changes.push_back(
                std::make_shared<Item_parent_change_operation>(
                    m_before_parent,
                    child,
                    std::shared_ptr<erhe::Hierarchy>{},
                    std::shared_ptr<erhe::Hierarchy>{}
                )
            );
        }
    } else {
        m_index_in_parent = m_index_in_parent_insert;
    }

    for (auto& child_parent_change : m_parent_changes) {
        child_parent_change->execute(context);
    }

    m_item->set_parent(m_after_parent, m_index_in_parent);

    context.selection->set_selection(m_selection_after);
}

void Item_insert_remove_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());

    erhe::Item_host_lock_guard scene_lock{m_item.get()};

    if (m_mode == Mode::remove) {
        m_after_parent = m_item->get_parent().lock();
    }
    m_item->set_parent(m_before_parent, m_index_in_parent);

    for (auto i = rbegin(m_parent_changes), end = rend(m_parent_changes); i < end; ++i) {
        auto& child_parent_change = *i;
        child_parent_change->undo(context);
    }

    context.selection->set_selection(m_selection_before);
}

}

