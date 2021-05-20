#include "operations/merge_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_manager.hpp"

namespace editor
{

Merge_operation::Merge_operation(Context& context)
    : m_context(context)
{
    if (context.selection_tool->selected_meshes().size() < 2)
    {
        return;
    }
    using namespace erhe::geometry;
    using namespace erhe::primitive;
    using namespace glm;

    Geometry                         combined_geometry;
    bool                             first_mesh                = true;
    mat4                             reference_node_from_world = mat4{1};
    Primitive_geometry::Normal_style normal_style              = Primitive_geometry::Normal_style::none;
    for (auto mesh : context.selection_tool->selected_meshes())
    {
        if (mesh.get() == nullptr)
        {
            continue;
        }
        VERIFY(mesh->node);
        mat4 transform;
        if (first_mesh)
        {
            reference_node_from_world = mesh->node->node_from_world();
            transform  = mat4{1};
            first_mesh = false;
        }
        else
        {
            transform = reference_node_from_world * mesh->node->world_from_node();
        }

        for (auto& primitive : mesh->primitives)
        {
            auto geometry = primitive.primitive_geometry->source_geometry;
            if (geometry.get() == nullptr)
            {
                continue;
            }
            combined_geometry.merge(*geometry, transform);
            if (normal_style == Primitive_geometry::Normal_style::none)
            {
                normal_style = primitive.primitive_geometry->source_normal_style;
            }
        }

        m_source_entries.emplace_back(mesh, mesh->primitives);
    }

    erhe::geometry::Geometry::Weld_settings weld_settings;
    combined_geometry.weld(weld_settings);
    combined_geometry.build_edges();

    m_combined_primitive_geometry = context.scene_manager->make_primitive_geometry(std::move(combined_geometry), normal_style);
    m_selection_before = context.selection_tool->selected_meshes();
    m_selection_after.push_back(context.selection_tool->selected_meshes().front());
}

void Merge_operation::execute()
{
    bool  first_entry = true;
    auto& meshes      = m_context.scene_manager->content_layer()->meshes;
    auto& nodes       = m_context.scene_manager->scene().nodes;
    for (const auto& entry : m_source_entries)
    {
        if (first_entry)
        {
            entry.mesh->primitives.resize(1);
            entry.mesh->primitives.front().primitive_geometry = m_combined_primitive_geometry;
            first_entry = false;
        }
        else
        {
            meshes.erase(std::remove(meshes.begin(), meshes.end(), entry.mesh), meshes.end());
            entry.mesh->node->reference_count--;
            if (entry.mesh->node->reference_count == 0)
            {
                nodes.erase(std::remove(nodes.begin(), nodes.end(), entry.mesh->node), nodes.end());
            }
        }
    }
    m_context.selection_tool->set_mesh_selection(m_selection_after);
}

void Merge_operation::undo()
{
    bool  first_entry = true;
    auto& meshes      = m_context.scene_manager->content_layer()->meshes;
    auto& nodes       = m_context.scene_manager->scene().nodes;
    for (const auto& entry : m_source_entries)
    {
        entry.mesh->primitives = entry.primitives;
        if (first_entry)
        {
            first_entry = false;
        }
        else
        {
            meshes.push_back(entry.mesh);
            if (entry.mesh->node->reference_count == 0)
            {
                nodes.push_back(entry.mesh->node);
            }
            entry.mesh->node->reference_count++;
        }
    }
    m_context.selection_tool->set_mesh_selection(m_selection_before);
}

} // namespace editor
