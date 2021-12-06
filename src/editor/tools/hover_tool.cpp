#include "hover_tool.hpp"
#include "log.hpp"
#include "rendering.hpp"
#include "tools.hpp"
#include "tools/hover_tool.hpp"
#include "tools/pointer_context.hpp"
#include "tools/trs_tool.hpp"
#include "renderers/line_renderer.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/scene_root.hpp"

#include "erhe/scene/mesh.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/toolkit/profile.hpp"

#include <fmt/core.h>
#include <fmt/format.h>
#include <imgui.h>

#include <string>

namespace editor
{

using namespace erhe::primitive;
using namespace erhe::geometry;

Hover_tool::Hover_tool()
    : erhe::components::Component{c_name}
{
}

Hover_tool::~Hover_tool() = default;

auto Hover_tool::description() -> const char*
{
    return c_description.data();
}

auto Hover_tool::state() const -> State
{
    return State::Passive;
}

void Hover_tool::connect()
{
    m_line_renderer   = get<Line_renderer>();
    m_pointer_context = get<Pointer_context>();
    m_scene_root      = require<Scene_root>();
    m_text_renderer   = get<Text_renderer>();
    require<Editor_tools>();
}

void Hover_tool::initialize_component()
{
    m_hover_material = m_scene_root->make_material("hover");
    m_hover_material_index = m_hover_material->index;
    get<Editor_tools>()->register_background_tool(this);
}

auto Hover_tool::tool_update() -> bool
{
    ERHE_PROFILE_FUNCTION

    if (m_pointer_context->window() == nullptr)
    {
        m_hover_content = false;
        m_hover_tool    = false;
        m_hover_position_world.reset();
        m_hover_normal.reset();
        deselect();
        return false;
    }

    m_hover_content        = m_pointer_context->hovering_over_content();
    m_hover_tool           = m_pointer_context->hovering_over_tool();
    m_hover_position_world = m_hover_content ? m_pointer_context->position_in_world() : std::optional<glm::vec3>{};
    m_hover_normal         = m_hover_content ? m_pointer_context->hover_normal()      : std::optional<glm::vec3>{};
    if (
        (m_pointer_context->hover_mesh()      != m_hover_mesh) ||
        (m_pointer_context->hover_primitive() != m_hover_primitive_index)
    )
    {
        deselect();
        select();
        return false;
    }
    return false;
}

void Hover_tool::tool_render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION

    const uint32_t text_color =
        m_hover_content ? 0xffffffffu :
        m_hover_tool    ? 0xffff0000u :
                          0xff0000ffu; // abgr

    if (m_hover_mesh != nullptr)
    {
        if (m_enable_color_highlight)
        {
            // float t = 0.5f + std::cos(render_context.time * glm::pi<float>()) * 0.5f;
            const float t = 1.0f;
            m_hover_material->emissive = m_original_primitive_material->emissive + t * m_hover_emissive;
        }

        if (
            (context.window != nullptr) &&
            m_hover_position_world.has_value()
        )
        {
            const auto position_in_viewport = context.window->project_to_viewport(m_hover_position_world.value());
            glm::vec3 position_at_fixed_depth{
                position_in_viewport.x + 50.0f,
                position_in_viewport.y,
                -0.5f
            };
            std::string text = fmt::format(
                "{}",
                m_hover_mesh->name()
            );
            m_text_renderer->print(
                position_at_fixed_depth,
                text_color,
                text
            );
        }

        if (
            m_hover_position_world.has_value() &&
            m_hover_normal.has_value()
        )
        {
            const glm::vec3 p0 = m_hover_position_world.value() + 0.1f * m_hover_normal.value();
            const glm::vec3 p1 = m_hover_position_world.value() + 1.1f * m_hover_normal.value();
            m_line_renderer->hidden.set_line_color(0xff0000ffu);
            m_line_renderer->hidden.add_lines(
                {
                    {
                        p0,
                        p1
                    }
                },
                10.0f
            );
        }
#if 0
        const glm::vec3 p0 = render_context.pointer_context->raytrace_hit_position;
        const glm::vec3 p1 = p0 + render_context.pointer_context->raytrace_hit_normal;
        const bool same_polygon = render_context.pointer_context->raytrace_local_index == render_context.pointer_context->hover_local_index;
        const uint32_t color = same_polygon ? 0xffff0000u : 0xff00ff00u;
        render_context.line_renderer->hidden.set_line_color(color);
        render_context.line_renderer->hidden.add_lines(
            {
                {
                    p0,
                    p1
                }
            },
            10.0f
        );
#endif
    }
}

void Hover_tool::deselect()
{
    ERHE_PROFILE_FUNCTION

    if (m_hover_mesh == nullptr)
    {
        return;
    }

    if (m_enable_color_highlight)
    {
        // Restore original primitive material
        m_hover_mesh->data.primitives[m_hover_primitive_index].material = m_original_primitive_material;
        m_original_primitive_material.reset();
        m_hover_mesh = nullptr;
    }
}
     
void Hover_tool::select()
{
    ERHE_PROFILE_FUNCTION

    if (m_pointer_context->hover_mesh() == nullptr)
    {
        return;
    }

    // Update hover information
    m_hover_mesh            = m_pointer_context->hover_mesh();
    m_hover_primitive_index = m_pointer_context->hover_primitive();

    if (m_enable_color_highlight)
    {
        // Store original primitive material
        m_original_primitive_material = m_hover_mesh->data.primitives[m_hover_primitive_index].material;

        // Copy material and mutate it with increased material emissive
        *m_hover_material.get()    = *m_original_primitive_material.get();
        m_hover_material->name     = "hover";
        m_hover_material->emissive = m_original_primitive_material->emissive + m_hover_emissive;
        m_hover_material->index    = m_hover_material_index;

        // Replace primitive material with modified copy
        m_hover_mesh->data.primitives[m_hover_primitive_index].material = m_hover_material;
        log_materials.info(
            "hover mesh {} primitive {} set to {} with index {}\n",
            m_hover_mesh->name(),
            m_hover_primitive_index,
            m_hover_material->name,
            m_hover_material->index
        );
    }
}

}
