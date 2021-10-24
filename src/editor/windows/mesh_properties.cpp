#include "windows/mesh_properties.hpp"
#include "tools.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/scene_manager.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"

#include "imgui.h"

using namespace erhe::geometry;

namespace editor
{

Mesh_properties::Mesh_properties()
    : erhe::components::Component(c_name)
{
}

Mesh_properties::~Mesh_properties() = default;

void Mesh_properties::connect()
{
    m_scene_manager  = get<Scene_manager >();
    m_scene_root     = get<Scene_root    >();
    m_selection_tool = get<Selection_tool>();
}

void Mesh_properties::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

auto Mesh_properties::state() const -> State
{
    return State::Passive;
}

void Mesh_properties::imgui(Pointer_context&)
{
    ImGui::Begin("Mesh");
    for (auto item : m_selection_tool->selection())
    {
        const auto mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(item);
        if (!mesh)
        {
            continue;
        }
        if (ImGui::TreeNode(mesh->name().c_str()))
        {
            ImGui::ColorEdit3("Wireframe Color", &mesh->wireframe_color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);

            const int max_primitive_index = static_cast<int>(mesh->primitives.size()) - 1;
            ImGui::SliderInt("Primitive", &m_primitive_index, 0, max_primitive_index);
            const auto& primitive = mesh->primitives.at(m_primitive_index);
            const auto  geometry  = primitive.primitive_geometry->source_geometry;
            if (geometry != nullptr)
            {
                if (ImGui::TreeNode(geometry->name.c_str()))
                {
                    ImGui::Text("Points: %d",   geometry->point_count());
                    ImGui::Text("Polygons: %d", geometry->polygon_count());
                    ImGui::Text("Edges: %d",    geometry->edge_count());
                    ImGui::Text("Corners: %d",  geometry->corner_count());

                    ImGui::Checkbox("Show Points",   &m_show_points);
                    ImGui::Checkbox("Show Polygons", &m_show_polygons);
                    ImGui::Checkbox("Show Edges",    &m_show_edges);
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::SliderInt("Max Labels", &m_max_labels, 0, 2000);
    ImGui::End();
}

void Mesh_properties::render(const Render_context& render_context)
{
    ZoneScoped;

    if (render_context.text_renderer == nullptr)
    {
        return;
    }

    //const auto& meshes = m_selection_tool->selected_meshes();
    //if (meshes.size() == 0)
    //{
    //    return;
    //}

    const auto*     camera          = m_scene_manager->get_view_camera().get();
    auto&           text_renderer   = *render_context.text_renderer;
    const glm::mat4 clip_from_world = camera->clip_from_world();
    for (auto item : m_selection_tool->selection())
    {
        const auto mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(item);
        if (!mesh)
        {
            continue;
        }
        const glm::mat4 world_from_node = mesh->node() ? mesh->node()->world_from_node() 
                                                       : glm::mat4(1.0f);
        for (auto& primitive : mesh->primitives)
        {
            const auto geometry = primitive.primitive_geometry->source_geometry;
            if (!geometry)
            {
                continue;
            }

            const auto polygon_centroids = geometry->polygon_attributes().find<glm::vec3>(c_polygon_centroids);
            const auto point_locations   = geometry->point_attributes  ().find<glm::vec3>(c_point_locations  );
            if ((point_locations != nullptr) && m_show_points)
            {
                const uint32_t end = std::min(static_cast<uint32_t>(m_max_labels), geometry->point_count());
                for (Point_id point_id = 0; point_id < end; ++point_id)
                {
                    if (!point_locations->has(point_id))
                    {
                        continue;
                    }
                    const auto p_in_node    = point_locations->get(point_id);
                    const auto p4_in_node   = glm::vec4{p_in_node, 1.0f};
                    const auto p4_in_world  = world_from_node * p4_in_node;
                    const auto p3_in_window = erhe::toolkit::project_to_screen_space(
                        clip_from_world,
                        glm::vec3{p4_in_world},
                        0.0f,
                        1.0f,
                        static_cast<float>(render_context.viewport.x),
                        static_cast<float>(render_context.viewport.y),
                        static_cast<float>(render_context.viewport.width),
                        static_cast<float>(render_context.viewport.height)
                    );
                    const uint32_t  text_color = 0xff00ff00u;
                    const glm::vec3 p3_in_window_z_negated{ p3_in_window.x,
                                                            p3_in_window.y,
                                                           -p3_in_window.z};
                    text_renderer.print(p3_in_window_z_negated, text_color, fmt::format("{}", point_id));
                }
            }

            if ((point_locations != nullptr) && m_show_edges)
            {
                for (Edge_id edge_id = 0; edge_id < geometry->edge_count(); ++edge_id)
                {
                    const auto& edge = geometry->edges[edge_id];

                    if (!point_locations->has(edge.a) || !point_locations->has(edge.b))
                    {
                        continue;
                    }
                    const auto p_in_node    = (point_locations->get(edge.a) + point_locations->get(edge.b)) / 2.0f;
                    const auto p4_in_node   = glm::vec4{p_in_node, 1.0f};
                    const auto p4_in_world  = world_from_node * p4_in_node;
                    const auto p3_in_window = erhe::toolkit::project_to_screen_space(
                        clip_from_world,
                        glm::vec3{p4_in_world},
                        0.0f,
                        1.0f,
                        static_cast<float>(render_context.viewport.x),
                        static_cast<float>(render_context.viewport.y),
                        static_cast<float>(render_context.viewport.width),
                        static_cast<float>(render_context.viewport.height)
                    );
                    const uint32_t  text_color = 0xff0000ffu;
                    const glm::vec3 p3_in_window_z_negated{
                        p3_in_window.x,
                        p3_in_window.y,
                        -p3_in_window.z
                    };
                    text_renderer.print(p3_in_window, text_color, fmt::format("{}", edge_id));
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
                    const glm::vec3 p_in_node    = polygon_centroids->get(polygon_id);
                    const glm::vec4 p4_in_node   = glm::vec4{p_in_node, 1.0f};
                    const glm::vec4 p4_in_world  = world_from_node * p4_in_node;
                    const glm::vec3 p3_in_window = erhe::toolkit::project_to_screen_space(
                        clip_from_world,
                        glm::vec3{p4_in_world},
                        0.0f,
                        1.0f,
                        static_cast<float>(render_context.viewport.x),
                        static_cast<float>(render_context.viewport.y),
                        static_cast<float>(render_context.viewport.width),
                        static_cast<float>(render_context.viewport.height)
                    );
                    const uint32_t text_color = 0xff00ffffu;
                    const glm::vec3 p3_in_window_reverse_z{p3_in_window.x, p3_in_window.y, -p3_in_window.z};
                    text_renderer.print(p3_in_window_reverse_z, text_color, fmt::format("{}", polygon_id));
                }
            }
        }
    }
}

}
