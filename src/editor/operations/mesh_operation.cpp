#include "operations/mesh_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_manager.hpp"

namespace editor
{

void Mesh_operation::execute()
{
    for (const auto& entry : m_entries)
    {
        erhe::scene::Mesh* mesh_ptr = entry.mesh.get();
        *mesh_ptr = entry.after;
    }
}

void Mesh_operation::undo()
{
    for (const auto& entry : m_entries)
    {
        erhe::scene::Mesh* mesh_ptr = entry.mesh.get();
        *mesh_ptr = entry.before;
    }
}

void Mesh_operation::make_entries(Context& context,
                                  std::function<erhe::geometry::Geometry(erhe::geometry::Geometry&)> operation)
{
    m_selection_tool = context.selection_tool;
    for (auto mesh : context.selection_tool->selected_meshes())
    {
        if (mesh.get() == nullptr)
        {
            continue;
        }
        Entry entry;
        entry.mesh   = mesh;
        entry.before = *entry.mesh;
        entry.after  = *entry.mesh;
        for (auto& primitive : entry.after.primitives)
        {
            auto geometry = primitive.primitive_geometry->source_geometry;
            if (geometry.get() == nullptr)
            {
                continue;
            }
            auto* g = geometry.get();
            auto& gr = *g;
            auto result_geometry = operation(gr);
            result_geometry.sanity_check();
            auto result_primitive_geometry = context.scene_manager->make_primitive_geometry(std::move(result_geometry),
                                                                                            primitive.primitive_geometry->source_normal_style);
            primitive.primitive_geometry = result_primitive_geometry;
        }
        add_entry(std::move(entry));
    }
}

void Mesh_operation::add_entry(Entry&& entry)
{
    m_entries.emplace_back(entry);
}

} // namespace editor
