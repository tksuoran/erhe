#include "operations/mesh_operation.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/scene.hpp"

#include <sstream>

namespace editor
{

Mesh_operation::Mesh_operation(Context&& context)
    : m_context{std::move(context)}
{
}

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

Mesh_operation::~Mesh_operation() = default;

void Mesh_operation::execute() const
{
    for (const auto& entry : m_entries)
    {
        m_context.scene.sanity_check();
        entry.mesh->data = entry.after;
        m_context.scene.sanity_check();
    }
}

void Mesh_operation::undo() const
{
    for (const auto& entry : m_entries)
    {
        m_context.scene.sanity_check();
        entry.mesh->data = entry.before;
        m_context.scene.sanity_check();
    }
}

void Mesh_operation::make_entries(
    const std::function<
        erhe::geometry::Geometry(erhe::geometry::Geometry&)
    > operation
)
{
    m_context.scene.sanity_check();

    m_selection_tool = m_context.selection_tool;
    for (auto item : m_context.selection_tool->selection())
    {
        const auto& mesh = as_mesh(item);
        if (!mesh)
        {
            continue;
        }

        Entry entry{
            .mesh   = mesh,
            .before = mesh->data,
            .after  = mesh->data
        };

        for (auto& primitive : entry.after.primitives)
        {
            auto geometry = primitive.source_geometry;
            if (geometry.get() == nullptr)
            {
                continue;
            }
            auto* g = geometry.get();
            auto& gr = *g;
            auto result_geometry = operation(gr);
            result_geometry.sanity_check();
            primitive.source_geometry = std::make_shared<erhe::geometry::Geometry>(
                std::move(result_geometry)
            );
            primitive.gl_primitive_geometry = make_primitive(
                *primitive.source_geometry.get(),
                m_context.build_info,
                primitive.normal_style
            );
        }
        add_entry(std::move(entry));
    }

    m_context.scene.sanity_check();
}

void Mesh_operation::add_entry(Entry&& entry)
{
    m_entries.emplace_back(entry);
}

} // namespace editor
