#include "operations/insert_operation.hpp"
#include "operations/node_operation.hpp"

#include "editor_log.hpp"
#include "scene/node_physics.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/scene/scene.hpp"
#include "erhe/scene/scene_host.hpp"

#include <sstream>

namespace editor
{

Node_transform_operation::Node_transform_operation(
    const Parameters& parameters
)
    : m_parameters{parameters}
{
}

Node_transform_operation::~Node_transform_operation() noexcept
{
}

auto Node_transform_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Node_transform " << m_parameters.node->get_name();
    return ss.str();
}

void Node_transform_operation::execute()
{
    log_operations->trace("Op Execute {}", describe());

    m_parameters.node->set_parent_from_node(m_parameters.parent_from_node_after);
}

void Node_transform_operation::undo()
{
    log_operations->trace("Op Undo {}", describe());

    m_parameters.node->set_parent_from_node(m_parameters.parent_from_node_before);
}

//

auto Node_insert_remove_operation::describe() const -> std::string
{
    ERHE_VERIFY(m_node);
    std::stringstream ss;
    switch (m_mode)
    {
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
    m_node             = parameters.node,
    m_selection_before = g_selection_tool->selection();

    if (parameters.mode == Mode::insert)
    {
        m_selection_after = g_selection_tool->selection();
        m_after_parent    = parameters.parent;
    }

    if (parameters.mode == Mode::remove)
    {
        m_node         = parameters.node;
        m_after_parent = std::shared_ptr<erhe::scene::Node>{};

        const auto& children = parameters.node->children();
        const auto parent = parameters.node->parent().lock();
        for (const auto& child : children)
        {
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

Node_insert_remove_operation::~Node_insert_remove_operation() noexcept
{
}

void Node_insert_remove_operation::execute()
{
    log_operations->trace("Op Execute {}", describe());

    {
        erhe::scene::Scene* const scene = m_node->get_scene();
        if (scene != nullptr)
        {
            scene->sanity_check();
        }
    }

    if (m_mode == Mode::remove)
    {
        m_before_parent = m_node->parent().lock();
        const auto& children = m_node->children();
        m_parent_changes.clear();
        for (const auto& child : children)
        {
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

    for (auto& child_parent_change : m_parent_changes)
    {
        child_parent_change->execute();
    }

    m_node->set_parent(m_after_parent);

    {
        erhe::scene::Scene* const scene = m_node->get_scene();
        if (scene != nullptr)
        {
            scene->sanity_check();
        }
    }

    if (g_selection_tool != nullptr)
    {
        g_selection_tool->set_selection(m_selection_after);
    }
}

void Node_insert_remove_operation::undo()
{
    log_operations->trace("Op Undo {}", describe());

    {
        erhe::scene::Scene* const scene = m_node->get_scene();
        if (scene != nullptr)
        {
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
        if (scene != nullptr)
        {
            scene->sanity_check();
        }
    }

    for (
        auto i = rbegin(m_parent_changes),
        end = rend(m_parent_changes);
        i < end;
        ++i
    )
    {
        auto& child_parent_change = *i;
        child_parent_change->undo();
    }

    if (g_selection_tool != nullptr)
    {
        g_selection_tool->set_selection(m_selection_before);
    }
}

} // namespace editor

