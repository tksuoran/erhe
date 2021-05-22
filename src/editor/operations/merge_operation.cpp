#include "operations/merge_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_manager.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/scene/scene.hpp"

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

    Geometry     combined_geometry;
    bool         first_mesh                = true;
    mat4         reference_node_from_world = mat4{1};
    Normal_style normal_style              = Normal_style::none;
    for (auto mesh : context.selection_tool->selected_meshes())
    {
        if (mesh.get() == nullptr)
        {
            continue;
        }
        auto node = mesh->node();
        VERIFY(node);
        mat4 transform;
        if (first_mesh)
        {
            reference_node_from_world = node->node_from_world();
            transform  = mat4{1};
            first_mesh = false;
        }
        else
        {
            transform = reference_node_from_world * node->world_from_node();
        }

        for (auto& primitive : mesh->primitives)
        {
            auto geometry = primitive.primitive_geometry->source_geometry;
            if (geometry.get() == nullptr)
            {
                continue;
            }
            combined_geometry.merge(*geometry, transform);
            if (normal_style == Normal_style::none)
            {
                normal_style = primitive.primitive_geometry->source_normal_style;
            }
        }

        m_source_entries.emplace_back(mesh, mesh->node(), mesh->primitives);
    }

    erhe::geometry::Geometry::Weld_settings weld_settings;
    combined_geometry.weld(weld_settings);
    combined_geometry.build_edges();

    m_combined_primitive_geometry = context.scene_manager->make_primitive_geometry(combined_geometry, normal_style);
    m_combined_primitive_geometry->source_geometry = std::make_shared<erhe::geometry::Geometry>(std::move(combined_geometry));
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
            // For first mesh: Replace mesh primitives
            entry.mesh->primitives.resize(1);
            entry.mesh->primitives.front().primitive_geometry = m_combined_primitive_geometry;
            first_entry = false;
        }
        else
        {
            // For other meshes:
            //  - Remove meshes from scene
            //  - Detach meshes from mesh nodes
            //  - If mesh node attachments become empty, remove node from scene
            auto node = entry.mesh->node();
            meshes.erase(std::remove(meshes.begin(), meshes.end(), entry.mesh), meshes.end());
            node->detach(entry.mesh);
            if (node->attachment_count() == 0)
            {
                nodes.erase(std::remove(nodes.begin(), nodes.end(), node), nodes.end());
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
        // For all entries:
        //  - Restore original mesh primitives
        entry.mesh->primitives = entry.primitives;
        if (first_entry)
        {
            first_entry = false;
        }
        else
        {
            // For non-first meshes:
            //  - Add mesh to scene
            //  - If node attachments were empty, add node to scene
            //  - Attach mesh to mesh node
            auto node = entry.mesh->node();
            meshes.push_back(entry.mesh);
            if (entry.node->attachment_count() == 0)
            {
                nodes.push_back(node);
            }
            entry.node->attach(entry.mesh);
        }
    }
    m_context.selection_tool->set_mesh_selection(m_selection_before);
}

} // namespace editor
