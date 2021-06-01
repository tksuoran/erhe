#include "operations/mesh_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_manager.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/primitive/primitive_builder.hpp"

namespace editor
{

using namespace std;
using namespace erhe::geometry;
using namespace erhe::primitive;
using namespace erhe::scene;

void Mesh_operation::execute()
{
    for (const auto& entry : m_entries)
    {
        Mesh* mesh_ptr = entry.mesh.get();
        *mesh_ptr = entry.after;
    }
}

void Mesh_operation::undo()
{
    for (const auto& entry : m_entries)
    {
        Mesh* mesh_ptr = entry.mesh.get();
        *mesh_ptr = entry.before;
    }
}

void Mesh_operation::make_entries(const Context&                      context,
                                  const function<Geometry(Geometry&)> operation)
{
    m_selection_tool = context.selection_tool;
    for (auto item : context.selection_tool->selection())
    {
        auto mesh = dynamic_pointer_cast<erhe::scene::Mesh>(item);
        if (!mesh)
        {
            continue;
        }
        Entry entry{context.layer,
                    context.scene,
                    context.physics_world,
                    mesh,
                    mesh->node(),
                    *entry.mesh,
                    *entry.mesh};
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
            auto result_primitive_geometry = make_primitive_shared(result_geometry,
                                                                   context.primitive_build_context,
                                                                   primitive.primitive_geometry->source_normal_style);
            primitive.primitive_geometry = result_primitive_geometry;
            primitive.primitive_geometry->source_geometry = make_shared<Geometry>(move(result_geometry));
        }
        add_entry(std::move(entry));
    }
}

void Mesh_operation::add_entry(Entry&& entry)
{
    m_entries.emplace_back(entry);
}

} // namespace editor
