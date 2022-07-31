#include "hover_tool.hpp"
#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "renderers/render_context.hpp"
#include "scene/material_library.hpp"
#include "scene/scene_root.hpp"
#include "tools/hover_tool.hpp"
#include "tools/tools.hpp"
#include "tools/trs_tool.hpp"
#include "windows/viewport_window.hpp"
#include "windows/viewport_windows.hpp"

#include "erhe/application/view.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <string>

namespace editor
{

using glm::vec3;

void Hover_tool_hover_command::on_inactive(
    erhe::application::Command_context& context
)
{
    static_cast<void>(context);

    m_hover_tool.on_inactive();
}

auto Hover_tool_hover_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    auto* viewport_window = as_viewport_window(context.get_window());
    if (viewport_window == nullptr)
    {
        set_inactive(context);
        return false;
    }

    return m_hover_tool.try_call();
}

Hover_tool::Hover_tool()
    : erhe::components::Component{c_label}
    , m_hover_command{*this}
{
}

Hover_tool::~Hover_tool() noexcept
{
}

auto Hover_tool::description() -> const char*
{
    return c_title.data();
}

void Hover_tool::declare_required_components()
{
    m_editor_scenes = require<Editor_scenes>();
    require<Tools>();
    require<erhe::application::View>();
    require<erhe::application::Imgui_windows>();
}

void Hover_tool::initialize_component()
{
    const auto& tools            = get<Tools>();
    auto*       tools_scene_root = tools->get_tool_scene_root();
    const auto& material_library = tools_scene_root->material_library();
    m_hover_material = material_library->make_material("hover");
    m_hover_material->visible = false;
    get<Tools>()->register_background_tool(this);

    const auto view = get<erhe::application::View>();
    view->register_command(&m_hover_command);
    view->bind_command_to_mouse_motion(&m_hover_command);
}

void Hover_tool::post_initialize()
{
    m_line_renderer_set = get<erhe::application::Line_renderer_set>();
    m_text_renderer     = get<erhe::application::Text_renderer    >();
    m_viewport_windows  = get<Viewport_windows                    >();
}

void Hover_tool::on_inactive()
{
    m_hover_content = false;
    m_hover_tool    = false;
    m_hover_position_world.reset();
    m_hover_normal.reset();
    deselect();
}

auto Hover_tool::try_call() -> bool
{
    Viewport_window* viewport_window = m_viewport_windows->hover_window();
    if (viewport_window == nullptr)
    {
        return false;
    }

    const auto& content = viewport_window->get_hover(Hover_entry::content_slot);
    const auto& tool    = viewport_window->get_hover(Hover_entry::tool_slot);
    m_hover_content        = content.valid && content.mesh;
    m_hover_tool           = tool   .valid && tool   .mesh;
    m_hover_position_world = content.valid ? content.position : nonstd::optional<vec3>{};
    m_hover_normal         = content.valid ? content.normal   : nonstd::optional<vec3>{};
    if (
        (content.mesh      != m_hover_mesh           ) ||
        (content.primitive != m_hover_primitive_index)
    )
    {
        deselect();
        select();
    }

    return false;
}

void Hover_tool::tool_render(
    const Render_context& context
)
{
    ERHE_PROFILE_FUNCTION

    auto& line_renderer = m_line_renderer_set->hidden;

    constexpr uint32_t red   = 0xff0000ffu;
    //constexpr uint32_t green = 0xff00ff00u;
    constexpr uint32_t blue  = 0xffff0000u;
    constexpr uint32_t white = 0xffffffffu;

    const uint32_t text_color =
        m_hover_content
            ? white
            : m_hover_tool
                ? blue
                : red;

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
            const auto position_in_viewport_opt = context.window->project_to_viewport(m_hover_position_world.value());
            if (position_in_viewport_opt.has_value())
            {
                const auto position_in_viewport = position_in_viewport_opt.value();
                const vec3 position_at_fixed_depth{
                    position_in_viewport.x + 50.0f,
                    position_in_viewport.y,
                    -0.5f
                };
                SPDLOG_LOGGER_TRACE(log_pointer, "P position in world: {}", m_hover_position_world.value());
                SPDLOG_LOGGER_TRACE(log_pointer, "P position in viewport: {}", position_in_viewport);
                const std::string text = fmt::format(
                    "{}",
                    m_hover_mesh->name()
                );
                m_text_renderer->print(
                    position_at_fixed_depth,
                    text_color,
                    text
                );
            }
        }

        if (
            m_hover_position_world.has_value() &&
            m_hover_normal.has_value()
        )
        {
            const vec3 p0 = m_hover_position_world.value() + 0.0f * m_hover_normal.value();
            const vec3 p1 = m_hover_position_world.value() + 1.0f * m_hover_normal.value();
            line_renderer.set_line_color(0xff0000ffu);
            line_renderer.set_thickness(10.0f);
            line_renderer.add_lines(
                {
                    {
                        p0,
                        p1
                    }
                }
            );
        }
#if 0
        Viewport_windows* viewport_window = m_viewport_windows()->hover_window();
        if (viewport_window->raytrace_hit_position().has_value())
        {
            const vec3 p0 = viewport_window->raytrace_hit_position().value();
            const vec3 neg_x = p0 - vec3{1.0f, 0.0f, 0.0f};
            const vec3 pos_x = p0 + vec3{1.0f, 0.0f, 0.0f};
            const vec3 neg_y = p0 - vec3{0.0f, 1.0f, 0.0f};
            const vec3 pos_y = p0 + vec3{0.0f, 1.0f, 0.0f};
            const vec3 neg_z = p0 - vec3{0.0f, 0.0f, 1.0f};
            const vec3 pos_z = p0 + vec3{0.0f, 0.0f, 1.0f};
            m_line_renderer_set->hidden.set_line_color(red);
            m_line_renderer_set->hidden.add_lines(
                {
                    {
                        neg_x,
                        pos_x
                    }
                },
                10.0f
            );
            m_line_renderer_set->hidden.set_line_color(green);
            m_line_renderer_set->hidden.add_lines(
                {
                    {
                        neg_y,
                        pos_y
                    }
                },
                10.0f
            );
            line_renderer.set_line_color(blue);
            line_renderer.add_lines(
                {
                    {
                        neg_z,
                        pos_z
                    }
                },
                10.0f
            );
        }
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
        m_hover_mesh->mesh_data.primitives[m_hover_primitive_index].material = m_original_primitive_material;
        m_original_primitive_material.reset();
        m_hover_mesh = nullptr;
    }
}

void Hover_tool::select()
{
    ERHE_PROFILE_FUNCTION

    Viewport_window* viewport_window = m_viewport_windows->hover_window();
    if (viewport_window == nullptr)
    {
        return;
    }

    const auto& hover = viewport_window->get_hover(Hover_entry::content_slot);
    if (!hover.valid || !hover.mesh)
    {
        return;
    }

    // Update hover information
    m_hover_mesh            = hover.mesh;
    m_hover_primitive_index = hover.primitive;

    if (m_enable_color_highlight)
    {
        // Store original primitive material
        m_original_primitive_material = m_hover_mesh->mesh_data.primitives[m_hover_primitive_index].material;

        // Copy material and mutate it with increased material emissive
        *m_hover_material.get()    = *m_original_primitive_material.get();
        m_hover_material->name     = "hover";
        m_hover_material->emissive = m_original_primitive_material->emissive + m_hover_emissive;
        m_hover_material->index    = m_hover_material_index;

        // Replace primitive material with modified copy
        m_hover_mesh->mesh_data.primitives[m_hover_primitive_index].material = m_hover_material;
        log_materials->info(
            "hover mesh {} primitive {} set to {} with index {}",
            m_hover_mesh->name(),
            m_hover_primitive_index,
            m_hover_material->name,
            m_hover_material->index
        );
    }
}

} // namespace editor
