#include "windows/mesh_properties.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/scene_manager.hpp"
#include "tools/selection_tool.hpp"
#include "erhe/scene/mesh.hpp"

#include "imgui.h"

using namespace erhe::geometry;

namespace sample
{

auto Mesh_properties::state() const -> State
{
    return State::passive;
}

Mesh_properties::Mesh_properties(const std::shared_ptr<Scene_manager>&  scene_manager,
                                 const std::shared_ptr<Selection_tool>& selection_tool)
    : m_scene_manager {scene_manager}
    , m_selection_tool{selection_tool}
{
}

void Mesh_properties::window(Pointer_context&)
{
    ImGui::Begin("Mesh Debug");
    const auto& meshes = m_selection_tool->selected_meshes();
    for (auto mesh : meshes)
    {
        if (mesh.get() == nullptr)
        {
            continue;
        }
        int max_primitive_index = static_cast<int>(mesh->primitives.size()) - 1;
        ImGui::SliderInt("Primitive", &m_primitive_index, 0, max_primitive_index);
        const auto& primitive = mesh->primitives.at(m_primitive_index);
        //auto* primitive_geometry = primitive.primitive_geometry.get();
        auto geometry = primitive.primitive_geometry->source_geometry;
        if (geometry != nullptr)
        {
            ImGui::Separator();
            ImGui::Text("%s",           geometry->name().c_str());
            ImGui::Text("Points: %d",   geometry->point_count());
            ImGui::Text("Polygons: %d", geometry->polygon_count());
            ImGui::Text("Edges: %d",    geometry->edge_count());
            ImGui::Text("Corners: %d",  geometry->corner_count());

            ImGui::Checkbox("Show Points",   &m_show_points);
            ImGui::Checkbox("Show Polygons", &m_show_polygons);
            ImGui::Checkbox("Show Edges",    &m_show_edges);
        }
        ImGui::Separator();
    }
    ImGui::SliderInt("Max Labels", &m_max_labels, 0, 2000);
    ImGui::End();
}

void Mesh_properties::render(Render_context& render_context)
{
    if (render_context.text_renderer == nullptr)
    {
        return;
    }
    const auto& meshes = m_selection_tool->selected_meshes();
    if (meshes.size() == 0)
    {
        return;
    }

    auto&     camera          = m_scene_manager->camera();
    auto&     text_renderer   = *render_context.text_renderer;
    glm::mat4 clip_from_world = camera.clip_from_world();
    for (auto mesh : meshes)
    {
        if (mesh.get() == nullptr)
        {
            continue;
        }
        glm::mat4 world_from_node = mesh->node ? mesh->node->world_from_node() : glm::mat4(1.0f);
        for (auto& primitive : mesh->primitives)
        {
            auto geometry = primitive.primitive_geometry->source_geometry;
            if (geometry.get() == nullptr)
            {
                continue;
            }

            auto polygon_centroids = geometry->polygon_attributes().find<glm::vec3>(c_polygon_centroids);
            auto point_locations   = geometry->point_attributes  ().find<glm::vec3>(c_point_locations  );
            if ((point_locations != nullptr) && m_show_points)
            {
                uint32_t end = geometry->point_count();
                end = std::min(static_cast<uint32_t>(m_max_labels), end);
                for (Point_id point_id = 0; point_id < end; ++point_id)
                {
                    if (!point_locations->has(point_id))
                    {
                        continue;
                    }
                    glm::vec3 p_in_node    = point_locations->get(point_id);
                    glm::vec4 p4_in_node   = glm::vec4(p_in_node, 1.0f);
                    glm::vec4 p4_in_world  = world_from_node * p4_in_node;
                    glm::vec3 p3_in_window = erhe::toolkit::project_to_screen_space(clip_from_world,
                                                                                    glm::vec3(p4_in_world),
                                                                                    0.0f,
                                                                                    1.0f,
                                                                                    static_cast<float>(render_context.viewport.x),
                                                                                    static_cast<float>(render_context.viewport.y),
                                                                                    static_cast<float>(render_context.viewport.width),
                                                                                    static_cast<float>(render_context.viewport.height));
                    uint32_t text_color = 0xff00ff00u;
                    p3_in_window.z = -p3_in_window.z;
                    text_renderer.print(fmt::format("{}", point_id), p3_in_window, text_color);
                }
            }

            if ((point_locations != nullptr) && m_show_edges)
            {
                for (Edge_id edge_id = 0; edge_id < geometry->edge_count(); ++edge_id)
                {
                    auto& edge = geometry->edges[edge_id];

                    if (!point_locations->has(edge.a) || !point_locations->has(edge.b))
                    {
                        continue;
                    }
                    glm::vec3 p_in_node    = (point_locations->get(edge.a) + point_locations->get(edge.b)) / 2.0f;
                    glm::vec4 p4_in_node   = glm::vec4(p_in_node, 1.0f);
                    glm::vec4 p4_in_world  = world_from_node * p4_in_node;
                    glm::vec3 p3_in_window = erhe::toolkit::project_to_screen_space(clip_from_world,
                                                                                    glm::vec3(p4_in_world),
                                                                                    0.0f,
                                                                                    1.0f,
                                                                                    static_cast<float>(render_context.viewport.x),
                                                                                    static_cast<float>(render_context.viewport.y),
                                                                                    static_cast<float>(render_context.viewport.width),
                                                                                    static_cast<float>(render_context.viewport.height));
                    uint32_t text_color = 0xff0000ffu;
                    p3_in_window.z = -p3_in_window.z;
                    text_renderer.print(fmt::format("{}", edge_id), p3_in_window, text_color);
                }
            }

            if ((polygon_centroids != nullptr) && m_show_polygons)
            {
                for (Polygon_id polygon_id = 0; polygon_id < geometry->polygon_count(); ++polygon_id)
                {
                    if (!polygon_centroids->has(polygon_id))
                    {
                        continue;
                    }
                    glm::vec3 p_in_node    = polygon_centroids->get(polygon_id);
                    glm::vec4 p4_in_node   = glm::vec4(p_in_node, 1.0f);
                    glm::vec4 p4_in_world  = world_from_node * p4_in_node;
                    glm::vec3 p3_in_window = erhe::toolkit::project_to_screen_space(clip_from_world,
                                                                                    glm::vec3(p4_in_world),
                                                                                    0.0f,
                                                                                    1.0f,
                                                                                    static_cast<float>(render_context.viewport.x),
                                                                                    static_cast<float>(render_context.viewport.y),
                                                                                    static_cast<float>(render_context.viewport.width),
                                                                                    static_cast<float>(render_context.viewport.height));
                    uint32_t text_color = 0xff00ffffu;
                    p3_in_window.z = -p3_in_window.z;
                    text_renderer.print(fmt::format("{}", polygon_id), p3_in_window, text_color);
                }
            }
        }
    }
}

}
