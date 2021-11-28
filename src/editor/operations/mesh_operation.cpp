#include "operations/mesh_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_manager.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/scene.hpp"

#include <sstream>

namespace editor
{

using namespace std;
using namespace erhe::geometry;
using namespace erhe::primitive;
using namespace erhe::scene;

auto Mesh_operation::describe() const -> std::string
{
    std::stringstream ss;
    bool first = true;
    for (const auto& entry : m_entries)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            ss << ", ";
        }
        ss << entry.mesh->name();
    }
    return ss.str();
}

Mesh_operation::Mesh_operation() = default;

Mesh_operation::~Mesh_operation() = default;

void Mesh_operation::execute() const
{
    for (const auto& entry : m_entries)
    {
        entry.scene.sanity_check();
        entry.mesh->data = entry.after;
        entry.scene.sanity_check();
    }
}

void Mesh_operation::undo() const
{
    for (const auto& entry : m_entries)
    {
        entry.scene.sanity_check();
        entry.mesh->data = entry.before;
        entry.scene.sanity_check();
    }
}

void Mesh_operation::make_entries(
    const Context&                                                      context,
    const function<erhe::geometry::Geometry(erhe::geometry::Geometry&)> operation
)
{
    context.scene.sanity_check();

    m_selection_tool = context.selection_tool;
    for (auto item : context.selection_tool->selection())
    {
        auto mesh = as_mesh(item);
        if (!mesh)
        {
            continue;
        }

        Entry entry{
            context.scene,
            context.layer,
            context.physics_world,
            mesh,       // mesh shared ptr
            mesh->data, // contents before
            mesh->data  // contents after
        };

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
            auto result_primitive_geometry = make_primitive_shared(
                result_geometry,
                context.build_info_set.gl,
                primitive.primitive_geometry->source_normal_style
            );
            primitive.primitive_geometry = result_primitive_geometry;
            primitive.primitive_geometry->source_geometry = make_shared<erhe::geometry::Geometry>(
                move(result_geometry)
            );
        }
        add_entry(std::move(entry));
    }

    context.scene.sanity_check();
}

void Mesh_operation::add_entry(Entry&& entry)
{
    m_entries.emplace_back(entry);
}

} // namespace editor
