#include "operations/mesh_operation.hpp"

#include "editor_log.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/scene.hpp"

#include <sstream>

namespace editor
{

Mesh_operation::Mesh_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
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

Mesh_operation::~Mesh_operation() noexcept
{
}

void Mesh_operation::execute(const Operation_context&)
{
    log_operations->trace("Op Execute {}", describe());

    for (const auto& entry : m_entries)
    {
        auto* scene_root = reinterpret_cast<Scene_root*>(entry.mesh->node_data.host);
        ERHE_VERIFY(scene_root);
        const auto& scene = scene_root->scene();
        scene.sanity_check();
        entry.mesh->mesh_data = entry.after;
        scene.sanity_check();
    }
}

void Mesh_operation::undo(const Operation_context&)
{
    log_operations->trace("Op Undo {}", describe());

    for (const auto& entry : m_entries)
    {
        auto* scene_root = reinterpret_cast<Scene_root*>(entry.mesh->node_data.host);
        ERHE_VERIFY(scene_root);
        const auto& scene = scene_root->scene();
        scene.sanity_check();
        entry.mesh->mesh_data = entry.before;
        scene.sanity_check();
    }
}

void Mesh_operation::make_entries(
    const std::function<
        erhe::geometry::Geometry(erhe::geometry::Geometry&)
    > operation
)
{
    const auto& selection = m_parameters.selection_tool->selection();
    if (selection.empty())
    {
        return;
    }
    auto* scene_root = reinterpret_cast<Scene_root*>(selection.front()->node_data.host);
    ERHE_VERIFY(scene_root);
    const auto& scene = scene_root->scene();
    scene.sanity_check();

    m_selection_tool = m_parameters.selection_tool;
    for (auto& item : m_parameters.selection_tool->selection())
    {
        const auto& mesh = as_mesh(item);
        if (!mesh)
        {
            continue;
        }

        Entry entry{
            .mesh   = mesh,
            .before = mesh->mesh_data,
            .after  = mesh->mesh_data
        };

        for (auto& primitive : entry.after.primitives)
        {
            const auto& geometry = primitive.source_geometry;
            auto* g = geometry.get();
            if (g == nullptr)
            {
                continue;
            }
            auto& gr = *g;
            auto result_geometry = operation(gr);
            result_geometry.sanity_check();
            primitive.source_geometry = std::make_shared<erhe::geometry::Geometry>(
                std::move(result_geometry)
            );
            primitive.gl_primitive_geometry = make_primitive(
                *primitive.source_geometry.get(),
                m_parameters.build_info,
                primitive.normal_style
            );
        }
        add_entry(std::move(entry));
    }

    scene.sanity_check();
}

void Mesh_operation::add_entry(Entry&& entry)
{
    m_entries.emplace_back(entry);
}

} // namespace editor
