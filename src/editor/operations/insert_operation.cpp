#include "operations/insert_operation.hpp"

#include "editor_context.hpp"
#include "operations/node_operation.hpp"

#include "editor_log.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/scene/scene.hpp"

#include <sstream>

namespace editor
{

auto Node_insert_remove_operation::describe() const -> std::string
{
    ERHE_VERIFY(m_node);
    std::stringstream ss;
    switch (m_mode) {
        //using enum Mode;
        case Mode::insert: ss << "Node_insert "; break;
        case Mode::remove: ss << "Node_remove "; break;
        default: break;
    }
    ss << m_node->get_name() << " ";
    return ss.str();
}

Node_insert_remove_operation::Node_insert_remove_operation(
    const Parameters& parameters
)
    : m_mode{parameters.mode}
{
    auto& selection = *parameters.context.selection;
    m_node             = parameters.node,
    m_selection_before = selection.get_selection();

    if (parameters.mode == Mode::insert) {
        m_selection_after = selection.get_selection();
        m_after_parent    = parameters.parent;
    }

    if (parameters.mode == Mode::remove) {
        m_after_parent = std::shared_ptr<erhe::scene::Node>{};

        const auto& children = parameters.node->children();
        const auto parent = parameters.node->parent().lock();
        for (const auto& child : children) {
            m_parent_changes.push_back(
                std::make_shared<Node_attach_operation>(
                    parent,
                    child,
                    std::shared_ptr<erhe::scene::Node>{},
                    std::shared_ptr<erhe::scene::Node>{}
                )
            );
        }

        log_tools->info("selection size = {}", m_selection_after.size());
    }
}

void Node_insert_remove_operation::execute(
    Editor_context& context
)
{
    log_operations->trace("Op Execute {}", describe());

    {
        erhe::scene::Scene* const scene = m_node->get_scene();
        if (scene != nullptr) {
            scene->sanity_check();
        }
    }

    if (m_mode == Mode::remove) {
        m_before_parent = m_node->parent().lock();
        const auto& children = m_node->children();
        m_parent_changes.clear();
        for (const auto& child : children) {
            log_tools->info("  child -> parent {}", child->get_name(), m_before_parent->get_name());
            m_parent_changes.push_back(
                std::make_shared<Node_attach_operation>(
                    m_before_parent,
                    child,
                    std::shared_ptr<erhe::scene::Node>{},
                    std::shared_ptr<erhe::scene::Node>{}
                )
            );
        }
    }

    for (auto& child_parent_change : m_parent_changes) {
        child_parent_change->execute(context);
    }

    m_node->set_parent(m_after_parent);

    {
        erhe::scene::Scene* const scene = m_node->get_scene();
        if (scene != nullptr) {
            scene->sanity_check();
        }
    }

    context.selection->set_selection(m_selection_after);
}

void Node_insert_remove_operation::undo(
    Editor_context& context
)
{
    log_operations->trace("Op Undo {}", describe());

    {
        erhe::scene::Scene* const scene = m_node->get_scene();
        if (scene != nullptr) {
            scene->sanity_check();
        }
    }

    if (m_mode == Mode::remove)
    {
        m_after_parent = m_node->parent().lock();
    }
    m_node->set_parent(m_before_parent);

    {
        erhe::scene::Scene* const scene = m_node->get_scene();
        if (scene != nullptr) {
            scene->sanity_check();
        }
    }

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

