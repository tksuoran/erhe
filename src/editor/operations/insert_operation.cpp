#include "operations/insert_operation.hpp"
#include "scene/scene_manager.hpp"

namespace editor
{

Node_transform_operation::Node_transform_operation(Context context)
    : m_context{context}
{
}

void Node_transform_operation::execute()
{
    m_context.node->transforms = m_context.after;
    // TODO update, handling trs tool node
}

void Node_transform_operation::undo()
{
    m_context.node->transforms = m_context.before;
}

Mesh_insert_remove_operation::Mesh_insert_remove_operation(Context& context)
    : m_context{context}
{
}

void Mesh_insert_remove_operation::execute()
{
    execute(m_context.mode);
}

void Mesh_insert_remove_operation::undo()
{
    execute(inverse(m_context.mode));
}

void Mesh_insert_remove_operation::execute(Mode mode)
{
    auto& items = m_context.scene_manager->content_layer()->meshes;
    auto& nodes = m_context.scene_manager->scene().nodes;
    if (mode == Mode::insert)
    {
        items.push_back(m_context.item);
        if (m_context.item->node)
        {
            if (m_context.item->node->reference_count == 0)
            {
                nodes.push_back(m_context.item->node);
            }
            m_context.item->node->reference_count++;
        }
    }
    else
    {
        items.erase(std::remove(items.begin(), items.end(), m_context.item), items.end());
        if (m_context.item->node)
        {
            m_context.item->node->reference_count--;
            if (m_context.item->node->reference_count == 0)
            {
                nodes.erase(std::remove(nodes.begin(), nodes.end(), m_context.item->node), nodes.end());
            }
        }
    }
}


Light_insert_remove_operation::Light_insert_remove_operation(Context& context)
    : m_context{context}
{
}

void Light_insert_remove_operation::execute()
{
    execute(m_context.mode);
}

void Light_insert_remove_operation::undo()
{
    execute(inverse(m_context.mode));
}

void Light_insert_remove_operation::execute(Mode mode)
{
    auto& items = m_context.scene_manager->content_layer()->lights;
    auto& nodes = m_context.scene_manager->scene().nodes;
    if (mode == Mode::insert)
    {
        items.push_back(m_context.item);
        if (m_context.item->node())
        {
            if (m_context.item->node()->reference_count == 0)
            {
                nodes.push_back(m_context.item->node());
            }
            m_context.item->node()->reference_count++;
        }
    }
    else
    {
        items.erase(std::remove(items.begin(), items.end(), m_context.item), items.end());
        if (m_context.item->node())
        {
            m_context.item->node()->reference_count--;
            if (m_context.item->node()->reference_count == 0)
            {
                nodes.erase(std::remove(nodes.begin(), nodes.end(), m_context.item->node()), nodes.end());
            }
        }
    }
}

}