#include "windows/mesh_properties.hpp"

#include "editor_rendering.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

#include <fmt/format.h>

namespace editor
{

using erhe::geometry::c_polygon_centroids;
using erhe::geometry::c_point_locations;
using erhe::geometry::Edge_id;
using erhe::geometry::Point_id;
using erhe::geometry::Polygon_id;

Mesh_properties::Mesh_properties()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title}
{
}

Mesh_properties::~Mesh_properties() noexcept
{
}

void Mesh_properties::declare_required_components()
{
    require<Tools>();
    require<erhe::application::Imgui_windows>();
}

void Mesh_properties::initialize_component()
{
    get<Tools>()->register_tool(this);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Mesh_properties::post_initialize()
{
    m_selection_tool = get<Selection_tool>();
    m_text_renderer  = get<erhe::application::Text_renderer>();
}

void Mesh_properties::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    ImGui::SliderInt("Max Labels",    &m_max_labels, 0, 2000);
    ImGui::Checkbox ("Show Points",   &m_show_points);
    ImGui::Checkbox ("Show Polygons", &m_show_polygons);
    ImGui::Checkbox ("Show Edges",    &m_show_edges);
#endif
}

void Mesh_properties::tool_render(
    const Render_context& context
)
{
    ERHE_PROFILE_FUNCTION

    if (!m_text_renderer || !m_selection_tool)
    {
        return;
    }

    const auto*     camera                = context.camera;
    const auto      projection_transforms = camera->projection_transforms(context.viewport);
    const glm::mat4 clip_from_world       = projection_transforms.clip_from_world.matrix();
    const auto&     selection             = m_selection_tool->selection();
    for (const auto& node : selection)
    {
        const auto* mesh = as_mesh(node.get());
        if (mesh == nullptr)
        {
            continue;
        }
        const glm::mat4 world_from_node = mesh->world_from_node();
        for (auto& primitive : mesh->mesh_data.primitives)
        {
            const auto& geometry = primitive.source_geometry;
            if (!geometry)
            {
                continue;
            }

            const auto polygon_centroids = geometry->polygon_attributes().find<glm::vec3>(c_polygon_centroids);
            const auto point_locations   = geometry->point_attributes  ().find<glm::vec3>(c_point_locations  );
            if ((point_locations != nullptr) && m_show_points)
            {
                const uint32_t end = (std::min)(
                    static_cast<uint32_t>(m_max_labels),
                    geometry->get_point_count()
                );
                for (Point_id point_id = 0; point_id < end; ++point_id)
                {
                    if (!point_locations->has(point_id))
                    {
                        continue;
                    }
                    const auto p_in_node    = point_locations->get(point_id);
                    const auto p4_in_node   = glm::vec4{p_in_node, 1.0f};
                    const auto p4_in_world  = world_from_node * p4_in_node;
                    const auto p3_in_window = context.viewport.project_to_screen_space(
                        clip_from_world,
                        glm::vec3{p4_in_world},
                        0.0f,
                        1.0f
                    );
                    const uint32_t  text_color = 0xff00ff00u;
                    const glm::vec3 p3_in_window_z_negated{
                         p3_in_window.x,
                         p3_in_window.y,
                        -p3_in_window.z
                    };
                    m_text_renderer->print(
                        p3_in_window_z_negated,
                        text_color,
                        fmt::format("{}", point_id)
                    );
                }
            }

            if ((point_locations != nullptr) && m_show_edges)
            {
                const uint32_t end = (std::min)(
                    static_cast<uint32_t>(m_max_labels),
                    geometry->get_edge_count()
                );

                for (Edge_id edge_id = 0; edge_id < end; ++edge_id)
                {
                    const auto& edge = geometry->edges[edge_id];

                    if (!point_locations->has(edge.a) || !point_locations->has(edge.b))
                    {
                        continue;
                    }
                    const auto p_in_node    = (point_locations->get(edge.a) + point_locations->get(edge.b)) / 2.0f;
                    const auto p4_in_node   = glm::vec4{p_in_node, 1.0f};
                    const auto p4_in_world  = world_from_node * p4_in_node;
                    const auto p3_in_window = context.viewport.project_to_screen_space(
                        clip_from_world,
                        glm::vec3{p4_in_world},
                        0.0f,
                        1.0f
                    );
                    const uint32_t  text_color = 0xff0000ffu;
                    const glm::vec3 p3_in_window_z_negated{
                         p3_in_window.x,
                         p3_in_window.y,
                        -p3_in_window.z
                    };
                    m_text_renderer->print(
                        p3_in_window_z_negated,
                        text_color,
                        fmt::format("{}", edge_id)
                    );
                }
            }

            if ((polygon_centroids != nullptr) && m_show_polygons)
            {
                const uint32_t end = (std::min)(
                    static_cast<uint32_t>(m_max_labels),
                    geometry->get_polygon_count()
                );
                for (Polygon_id polygon_id = 0; polygon_id < end; ++polygon_id)
                {
                    if (!polygon_centroids->has(polygon_id))
                    {
                        continue;
                    }
                    const glm::vec3 p_in_node    = polygon_centroids->get(polygon_id);
                    const glm::vec4 p4_in_node   = glm::vec4{p_in_node, 1.0f};
                    const glm::vec4 p4_in_world  = world_from_node * p4_in_node;
                    const glm::vec3 p3_in_window = context.viewport.project_to_screen_space(
                        clip_from_world,
                        glm::vec3{p4_in_world},
                        0.0f,
                        1.0f
                    );
                    const uint32_t  text_color = 0xff00ffffu;
                    const glm::vec3 p3_in_window_z_negated{
                         p3_in_window.x,
                         p3_in_window.y,
                        -p3_in_window.z
                    };
                    m_text_renderer->print(
                        p3_in_window_z_negated,
                        text_color,
                        fmt::format("{}", polygon_id)
                    );
                }
            }
        }
    }
}

}
