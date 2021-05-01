#include "tools/hover_tool.hpp"
#include "tools/pointer_context.hpp"
#include "tools/trs_tool.hpp"
#include "renderers/line_renderer.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/scene_manager.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe_tracy.hpp"
#include "log.hpp"

#include "imgui.h"

namespace sample
{

using namespace erhe::primitive;
using namespace erhe::geometry;

auto Hover_tool::state() const -> Tool::State
{
    return State::passive;
}

Hover_tool::Hover_tool(const std::shared_ptr<Scene_manager>& scene_manager)

    : m_scene_manager{scene_manager}
{
    m_hover_material = std::make_shared<erhe::primitive::Material>();
    m_hover_material->name = "hover";
    scene_manager->add(m_hover_material);
    m_hover_material_index = m_hover_material->index;
}

auto Hover_tool::update(Pointer_context& pointer_context) -> bool
{
    ZoneScoped;

    m_hover_position = glm::vec3(pointer_context.pointer_x,
                                 pointer_context.pointer_y,
                                 pointer_context.pointer_z);
    m_hover_content        = pointer_context.hover_content;
    m_hover_tool           = pointer_context.hover_tool;
    m_hover_position_world = m_hover_content && pointer_context.hover_valid ? pointer_context.position_in_world() : std::optional<glm::vec3>{};
    m_hover_normal         = m_hover_content && pointer_context.hover_valid ? pointer_context.hover_normal        : std::optional<glm::vec3>{};
    if ((pointer_context.hover_mesh      != m_hover_mesh) ||
        (pointer_context.hover_primitive != m_hover_primitive_index))
    {
        deselect();
        select(pointer_context);
        return false;
    }
    return false;
}

void Hover_tool::render(Render_context& render_context)
{
    uint32_t text_color = m_hover_content ? 0xffffffffu :
                          m_hover_tool    ? 0xffff0000u :
                                            0xff0000ffu; // abgr
    if (m_hover_mesh != nullptr)
    {
        if (m_enable_color_highlight)
        {
            // float t = 0.5f + std::cos(render_context.time * glm::pi<float>()) * 0.5f;
            float t = 1.0f;
            m_hover_material->emissive = m_original_primitive_material->emissive + t * m_hover_emissive;
        }

        if (m_hover_position.has_value())
        {
            auto position = m_hover_position.value();
            position.x += 50.0f;
            position.z  = -0.5f;
            render_context.text_renderer->print(m_hover_mesh->name,
                                                position,
                                                text_color);
        }
        if (m_hover_position_world.has_value() && m_hover_normal.has_value())
        {
            glm::vec3 p0 = m_hover_position_world.value();
            glm::vec3 p1 = m_hover_position_world.value() + m_hover_normal.value();
            render_context.line_renderer->set_line_color(0xffffffffu);
            render_context.line_renderer->add_lines( { {p0, p1} }, 10.0f);
        }
    }
}

void Hover_tool::deselect()
{
    ZoneScoped;

    if (m_hover_mesh == nullptr)
    {
        return;
    }

    if (m_enable_color_highlight)
    {
        // Restore original primitive material
        m_hover_mesh->primitives[m_hover_primitive_index].material = m_original_primitive_material;
        m_original_primitive_material.reset();
        m_hover_mesh = nullptr;
    }
}
     
void Hover_tool::select(Pointer_context& pointer_context)
{
    ZoneScoped;

    if (pointer_context.hover_mesh == nullptr)
    {
        return;
    }

    // Update hover information
    m_hover_mesh            = pointer_context.hover_mesh;
    m_hover_primitive_index = pointer_context.hover_primitive;

    if (m_enable_color_highlight)
    {
        // Store original primitive material
        m_original_primitive_material = m_hover_mesh->primitives[m_hover_primitive_index].material;

        // Copy material and mutate it with increased material emissive
        *m_hover_material.get()    = *m_original_primitive_material.get();
        m_hover_material->name     = "hover";
        m_hover_material->emissive = m_original_primitive_material->emissive + m_hover_emissive;
        m_hover_material->index    = m_hover_material_index;

        // Replace primitive material with modified copy
        m_hover_mesh->primitives[m_hover_primitive_index].material = m_hover_material;
        log_materials.info("hover mesh {} primitive {} set to {} with index {}\n",
                            m_hover_mesh->name,
                            m_hover_primitive_index,
                            m_hover_material->name,
                            m_hover_material->index);
    }
}

void Hover_tool::window(Pointer_context&)
{
    ZoneScoped;

    ImGui::Begin("Hover");
    if (m_hover_mesh != nullptr)
    {
        ImGui::Text("Mesh: %s", m_hover_mesh->name.c_str());
        auto* node = m_hover_mesh->node;
        if (node != nullptr)
        {
            ImGui::Text("Node: %s", node->name.c_str());
        }
    }
    ImGui::ColorEdit4("Highlight Color", &m_hover_emissive.x, ImGuiColorEditFlags_Float);
    ImGui::End();
}

}