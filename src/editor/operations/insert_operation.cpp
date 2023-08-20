#include "operations/insert_operation.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "operations/node_operation.hpp"
#include "tools/selection_tool.hpp"

#include <sstream>

namespace editor
{

auto Item_insert_remove_operation::describe() const -> std::string
{
    ERHE_VERIFY(m_item);
    std::stringstream ss;
    switch (m_mode) {
        //using enum Mode;
        case Mode::insert: ss << "Item_insert "; break;
        case Mode::remove: ss << "Item_remove "; break;
        default: break;
    }
    ss << m_item->get_name() << " ";
    return ss.str();
}

Item_insert_remove_operation::Item_insert_remove_operation(const Parameters& parameters)
    : m_mode{parameters.mode}
{
    auto& selection = *parameters.context.selection;
    m_item             = parameters.item,
    m_selection_before = selection.get_selection();

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

void Item_insert_remove_operation::execute(Editor_context& context)
{
    log_operations->trace("Op Execute {}", describe());

    if (m_mode == Mode::remove) {
        m_before_parent = m_item->get_parent().lock();
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
    }

    for (auto& child_parent_change : m_parent_changes) {
        child_parent_change->execute(context);
    }

    m_item->set_parent(m_after_parent);

    context.selection->set_selection(m_selection_after);
}

void Item_insert_remove_operation::undo(Editor_context& context)
{
    log_operations->trace("Op Undo {}", describe());

    if (m_mode == Mode::remove) {
        m_after_parent = m_item->get_parent().lock();
    }
    m_item->set_parent(m_before_parent);

    for (
        auto i = rbegin(m_parent_changes),
        end = rend(m_parent_changes);
        i < end;
        ++i
    ) {
        auto& child_parent_change = *i;
        child_parent_change->undo(context);
    }

    context.selection->set_selection(m_selection_before);
}

} // namespace editor

