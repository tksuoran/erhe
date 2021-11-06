#include "operations/merge_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_manager.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/scene.hpp"

#include <memory>

namespace editor
{

Merge_operation::Merge_operation(Context& context)
    : m_context(context)
{
    if (context.selection_tool->selection().size() < 2)
    {
        return;
    }

    using namespace erhe::geometry;
    using namespace erhe::primitive;
    using namespace glm;

    erhe::geometry::Geometry combined_geometry;
    bool first_mesh                = true;
    mat4 reference_node_from_world = mat4{1};
    auto normal_style              = Normal_style::none;
    for (auto item : context.selection_tool->selection())
    {
        auto mesh = as_mesh(item);
        if (!mesh)
        {
            continue;
        }
        mat4 transform;
        if (first_mesh)
        {
            reference_node_from_world = mesh->node_from_world();
            transform  = mat4{1};
            first_mesh = false;
        }
        else
        {
            transform = reference_node_from_world * mesh->world_from_node();
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

        m_source_entries.emplace_back(
            context.build_info_set,
            context.scene,
            context.layer,
            context.physics_world,
            mesh,
            mesh->primitives
        );
    }

    if (m_source_entries.size() < 2)
    {
        return;
    }

    const erhe::geometry::Geometry::Weld_settings weld_settings;
    combined_geometry.weld(weld_settings);
    combined_geometry.build_edges();

    m_combined_primitive_geometry = make_primitive_shared(combined_geometry,
                                                          context.build_info_set.gl,
                                                          normal_style);
    m_combined_primitive_geometry->source_geometry     = std::make_shared<erhe::geometry::Geometry>(std::move(combined_geometry));
    m_combined_primitive_geometry->source_normal_style = normal_style;
    m_selection_before = context.selection_tool->selection();
    m_selection_after = {};
}

void Merge_operation::execute()
{
    bool  first_entry = true;
    auto& meshes      = m_context.layer.meshes;
    auto& nodes       = m_context.scene.nodes;
    for (const auto& entry : m_source_entries)
    {
        auto mesh = entry.mesh;
        if (first_entry)
        {
            // For first mesh: Replace mesh primitives
            mesh->primitives.resize(1);
            mesh->primitives.front().primitive_geometry = m_combined_primitive_geometry;
            first_entry = false;
        }
        else
        {
            // For other meshes:
            //  - Remove meshes from scene
            //  - Detach meshes from mesh nodes
            meshes.erase(
                std::remove(
                    meshes.begin(),
                    meshes.end(),
                    mesh
                ),
                meshes.end()
            );
            mesh->unparent();
            nodes.erase(
                std::remove(
                    nodes.begin(),
                    nodes.end(),
                    mesh
                ),
                nodes.end()
            );
        }
    }
    m_context.selection_tool->set_selection(m_selection_after);
}

void Merge_operation::undo()
{
    bool  first_entry     = true;
    auto& meshes          = m_context.layer.meshes;
    auto& transform_nodes = m_context.scene.nodes;
    for (const auto& entry : m_source_entries)
    {
        // For all entries:
        //  - Restore original mesh primitives
        auto mesh = entry.mesh;
        mesh->primitives = entry.primitives;
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
            meshes.push_back(mesh);
            transform_nodes.push_back(mesh);
            mesh->unparent();
        }
    }
    m_context.scene.nodes_sorted = false;
    m_context.selection_tool->set_selection(m_selection_before);
}

} // namespace editor
