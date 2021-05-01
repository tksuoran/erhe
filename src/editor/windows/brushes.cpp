#include "windows/brushes.hpp"
#include "tools/pointer_context.hpp"
#include "scene/scene_manager.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/mesh.hpp"

#include "imgui.h"

namespace sample
{

using namespace std;
using namespace erhe::primitive;
using namespace erhe::toolkit;

auto Brushes::state() const -> Tool::State
{
    return m_state;
}

Brushes::Brushes(const shared_ptr<Scene_manager>& scene_manager)
    : m_scene_manager(scene_manager)
{
    const auto& materials = scene_manager->materials();
    for (auto material : materials)
    {
        add_material(material);
    }

    m_brushes.push_back({});
    m_brush_names.push_back("-");
    const auto& primitive_geometries = scene_manager->primitive_geometries();
    for (auto primitive_geometry : primitive_geometries)
    {
        add_brush(primitive_geometry);
    }
}

void Brushes::add_brush(const shared_ptr<Primitive_geometry>& primitive_geometry)
{
    m_brushes.push_back(primitive_geometry);
    auto* geometry = primitive_geometry->source_geometry.get();
    const char* name = (geometry != nullptr) ? geometry->name().c_str() : "";
    m_brush_names.push_back(name);
}

void Brushes::add_material(const shared_ptr<Material>& material)
{
    m_materials.push_back(material);
    m_material_names.push_back(material->name.c_str());
}

auto Brushes::update(Pointer_context& pointer_context) -> bool
{
    m_hover_content  = pointer_context.hover_content;
    m_hover_tool     = pointer_context.hover_tool;
    m_hover_position = m_hover_content && pointer_context.hover_valid ? pointer_context.position_in_world() : std::optional<glm::vec3>{};
    m_hover_normal   = m_hover_content && pointer_context.hover_valid ? pointer_context.hover_normal        : std::optional<glm::vec3>{};

    if ((m_state == State::passive) &&
        pointer_context.mouse_button[Mouse_button_left].pressed &&
        m_brush_mesh)
    {
        m_state = State::ready;
        return true;
    }

    if (m_state != State::ready)
    {
        return false;
    }

    if (pointer_context.mouse_button[Mouse_button_left].released && m_brush_mesh)
    {
        m_brush_mesh->visibility_mask |= erhe::scene::Mesh::c_visibility_id;
        m_brush_mesh.reset();
        m_state = State::passive;
        return true;
    }

    return false;
}

void Brushes::render(Render_context& render_context)
{
    static_cast<void>(render_context);
//    ZoneScoped;
//
//    if (m_selected_brush == 0)
//    {
//        return;
//    }
//
//    if (m_hover_content && m_hover_position.has_value() && m_hover_normal.has_value())
//    {
//        const auto& meshes = m_scene_manager->content_layer()->meshes;
//    }
}

void Brushes::render_update()
{
    update_mesh();
}

void Brushes::update_mesh_node_transform()
{
    if (!m_hover_position.has_value())
    {
        return;
    }
    auto node = m_brush_mesh->node;
    if (node == nullptr)
    {
        // unexpected
        return;
    }

    glm::vec3 N = m_hover_normal.has_value() ? m_hover_normal.value() : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 T = erhe::toolkit::min_axis(N);
    glm::vec3 B = glm::normalize(glm::cross(T, N));
              N = glm::normalize(glm::cross(B, T));
              T = glm::normalize(glm::cross(N, B));

    glm::vec3 P = m_hover_position.value();
    auto primitive_geometry = m_brushes[m_selected_brush];
    glm::vec3 offset{0.0f, primitive_geometry->bounding_box_min.y, 0.0f};
    glm::mat4 translate = create_translation(-offset);
    glm::mat4 scale     = create_scale(m_scale);
    glm::mat4 rotate;
    rotate[0][0] = T[0]; rotate[1][0] = T[1]; rotate[2][0] = T[2]; rotate[3][0] = 0.0f;
    rotate[0][1] = N[0]; rotate[1][1] = N[1]; rotate[2][1] = N[2]; rotate[3][1] = 0.0f;
    rotate[0][2] = B[0]; rotate[1][2] = B[1]; rotate[2][2] = B[2]; rotate[3][2] = 0.0f;
    rotate[0][3] = 0.0f; rotate[1][3] = 0.0f; rotate[2][3] = 0.0f; rotate[3][3] = 1.0f;
    rotate = glm::transpose(rotate);
    glm::mat4 translate_to_world = create_translation(P);

    node->transforms.parent_from_node.set(translate_to_world * rotate * scale * translate);
}

void Brushes::update_mesh()
{
    auto primitive_geometry = m_brushes[m_selected_brush];
    if (!m_brush_mesh)
    {
        if ((m_selected_brush == 0) || !m_hover_position.has_value())
        {
            return;
        }
        std::string name = m_brush_names[m_selected_brush];
        auto material = m_materials[m_selected_material];
        m_brush_mesh = m_scene_manager->make_mesh_node(name, primitive_geometry, material);
        m_brush_mesh->visibility_mask &= ~(erhe::scene::Mesh::c_visibility_id);// |
                                           //erhe::scene::Mesh::c_visibility_shadow_cast);
        update_mesh_node_transform();
        return;
    }

    auto layer = m_scene_manager->content_layer();
    auto& meshes = layer->meshes;
    if ((m_selected_brush == 0) || !m_hover_position.has_value())
    {
        auto i = std::remove(meshes.begin(), meshes.end(), m_brush_mesh);
        if (i != meshes.end())
        {
            meshes.erase(i, meshes.end());
        }
        m_brush_mesh.reset();
        return;
    }

    update_mesh_node_transform();
}

void Brushes::window(Pointer_context&)
{
    ImGui::Begin("Brushes");

    if (!m_brush_names.empty())
    {
        ImGui::Combo("Brush", &m_selected_brush,
                     m_brush_names.data(), static_cast<int>(m_brush_names.size()));
    }
    if (!m_material_names.empty())
    {
        ImGui::Combo("Material", &m_selected_material,
                     m_material_names.data(), static_cast<int>(m_material_names.size()));
    }
    ImGui::SliderFloat("Scale", &m_scale, 0.0f, 2.0f);
    ImGui::End();
}

}
