// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "hover_tool.hpp"
#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "renderers/render_context.hpp"
#include "scene/material_library.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/hover_tool.hpp"
#include "tools/tools.hpp"
#include "tools/trs_tool.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/application/view.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/physics/irigid_body.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/profile.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <string>

namespace editor
{

Hover_tool::Hover_tool()
    : erhe::application::Imgui_window{c_title}
    , erhe::components::Component{c_type_name}
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
    require<erhe::application::Imgui_windows>();
    //m_editor_scenes = require<Editor_scenes>();
    require<Tools>();
}

void Hover_tool::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const auto& imgui_windows = get<erhe::application::Imgui_windows>();
    imgui_windows->register_imgui_window(this);

    const auto& tools = get<Tools>();
    const auto& tools_scene_root = tools->get_tool_scene_root().lock();
    if (!tools_scene_root)
    {
        return;
    }
    //// const auto& material_library = tools_scene_root->material_library();
    //// m_hover_material = material_library->make_material("hover");
    //// m_hover_material->visible = false;
    tools->register_background_tool(this);
    tools->register_hover_tool(this);
}

void Hover_tool::post_initialize()
{
    m_line_renderer_set = get<erhe::application::Line_renderer_set>();
    m_text_renderer     = get<erhe::application::Text_renderer    >();
    m_viewport_windows  = get<Viewport_windows                    >();
}

//// void Hover_tool::on_inactive()
//// {
////     m_hover_content = false;
////     m_hover_tool    = false;
////     m_hover_position_world.reset();
////     m_hover_normal.reset();
////     deselect();
//// }

//// void Hover_tool::on_scene_view(Scene_view* scene_view)
//// {
////     if (scene_view == nullptr)
////     {
////         on_inactive();
////         return;
////     }
////
////     const auto& content = scene_view->get_hover(Hover_entry::content_slot);
////     const auto& tool    = scene_view->get_hover(Hover_entry::tool_slot);
////     m_hover_content        = content.valid && content.mesh;
////     m_hover_tool           = tool   .valid && tool   .mesh;
////     m_hover_position_world = content.valid ? content.position : std::optional<vec3>{};
////     m_hover_normal         = content.valid ? content.normal   : std::optional<vec3>{};
////     if (
////         (content.mesh      != m_hover_mesh           ) ||
////         (content.primitive != m_hover_primitive_index)
////     )
////     {
////         deselect();
////         select();
////     }
//// }

void Hover_tool::tool_hover(Scene_view* scene_view)
{
    if (scene_view == nullptr)
    {
        for (std::size_t slot = 0; slot < Hover_entry::slot_count; ++slot)
        {
            m_hover_entries[slot] = Hover_entry{};
        }
        return;
    }

    for (std::size_t slot = 0; slot < Hover_entry::slot_count; ++slot)
    {
        m_hover_entries[slot] = scene_view->get_hover(slot);
    }
    m_origin    = scene_view->get_control_ray_origin_in_world();
    m_direction = scene_view->get_control_ray_direction_in_world();
}

void Hover_tool::imgui()
{
    const auto hover        = m_hover_entries[Hover_entry::content_slot];
    const auto tool         = m_hover_entries[Hover_entry::tool_slot];
    const auto rendertarget = m_hover_entries[Hover_entry::rendertarget_slot];
    ImGui::Text("Content: %s",      (hover.valid && hover.mesh) ? hover.mesh->name().c_str() : "");
    ImGui::Text("Tool: %s",         (tool.valid && tool.mesh) ? tool.mesh->name().c_str() : "");
    ImGui::Text("Rendertarget: %s", (rendertarget.valid && rendertarget.mesh) ? rendertarget.mesh->name().c_str() : "");
    if (m_origin.has_value())
    {
        const glm::dvec3 o = m_origin.value();
        ImGui::Text("Ray Origin: %2.4f, %2.4f, %2.f", o.x, o.y, o.z);
    }
    if (m_direction.has_value())
    {
        const glm::dvec3 d = m_direction.value();
        ImGui::Text("Ray Direction: %2.4f, %2.4f, %2.f", d.x, d.y, d.z);
    }
}

void Hover_tool::tool_render(
    const Render_context& context
)
{
    ERHE_PROFILE_FUNCTION

    if (context.scene_view == nullptr)
    {
        return;
    }

    const auto& content       = context.scene_view->get_hover(Hover_entry::content_slot);
    const auto& tool          = context.scene_view->get_hover(Hover_entry::tool_slot);
    const bool  hover_content = content.valid && content.mesh;
    const bool  hover_tool    = tool   .valid && tool   .mesh;
    const std::optional<glm::dvec3> hover_position_world = content.valid ? content.position : std::optional<glm::dvec3>{};

    auto& line_renderer = *m_line_renderer_set->hidden.at(2).get();

    constexpr uint32_t red   = 0xff0000ffu;
    constexpr uint32_t blue  = 0xffff0000u;
    constexpr uint32_t white = 0xffffffffu;

    const uint32_t text_color =
        hover_content
            ? white
            : hover_tool
                ? blue
                : red;

    if (content.mesh != nullptr)
    {
        if (
            (context.viewport_window != nullptr) &&
            hover_position_world.has_value()
        )
        {
            const auto position_in_viewport_opt = context.viewport_window->project_to_viewport(hover_position_world.value());
            if (position_in_viewport_opt.has_value())
            {
                const auto position_in_viewport = position_in_viewport_opt.value();
                const glm::vec3 position_at_fixed_depth{
                    position_in_viewport.x + 50.0f,
                    position_in_viewport.y,
                    -0.5f
                };

                SPDLOG_LOGGER_TRACE(log_pointer, "P position in world: {}", m_hover_position_world.value());
                SPDLOG_LOGGER_TRACE(log_pointer, "P position in viewport: {}", position_in_viewport);

                const std::string text = fmt::format(
                    "{}",
                    content.mesh->name()
                );

                m_text_renderer->print(
                    position_at_fixed_depth,
                    text_color,
                    text
                );

                if (hover_position_world.has_value())
                {
                    const glm::vec3 position_at_fixed_depth_line_2{
                        position_in_viewport.x + 50.0f,
                        position_in_viewport.y + 16.0f,
                        -0.5f
                    };
                    const std::string text_line_2 = fmt::format(
                        "Position in world: {}",
                        hover_position_world.value()
                    );
                    m_text_renderer->print(
                        position_at_fixed_depth_line_2,
                        text_color,
                        text_line_2
                    );
                }

                auto node_physics = get_physics_node(content.mesh.get());
                if (node_physics)
                {
                    erhe::physics::IRigid_body* rigid_body = node_physics->rigid_body();
                    if (rigid_body)
                    {
                        const glm::vec3 position_at_fixed_depth_line_3{
                            position_in_viewport.x + 50.0f,
                            position_in_viewport.y + 16.0f * 2,
                            -0.5f
                        };
                        const glm::vec3 local_position = content.mesh->transform_point_from_world_to_local(hover_position_world.value());
                        const std::string text_line_3 = fmt::format(
                            "Position in {}: {}",
                            content.mesh->name(),
                            local_position
                        );

                        //const int motion_mode_index = static_cast<int>(rigid_body->get_motion_mode());
                        m_text_renderer->print(
                            position_at_fixed_depth_line_3,
                            text_color,
                            text_line_3.c_str() // erhe::physics::c_motion_mode_strings[motion_mode_index]
                        );
                    }
                }

            }
        }

        if (
            hover_position_world.has_value() &&
            content.normal.has_value()
        )
        {
            const auto p0 = hover_position_world.value();
            const auto p1 = hover_position_world.value() + content.normal.value();
            line_renderer.set_thickness(10.0f);
            line_renderer.add_lines(
                glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
                {
                    {
                        glm::vec3{p0},
                        glm::vec3{p1}
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

//[[nodiscard]] auto Hover_tool::viewport_window() const -> std::shared_ptr<Viewport_window>
//{
//    return m_viewport_windows->hover_window();
//}

//// void Hover_tool::deselect()
//// {
////     ERHE_PROFILE_FUNCTION
////
////     if (m_hover_mesh == nullptr)
////     {
////         return;
////     }
////
////     if (m_enable_color_highlight)
////     {
////         // Restore original primitive material
////         m_hover_mesh->mesh_data.primitives[m_hover_primitive_index].material = m_original_primitive_material;
////         m_original_primitive_material.reset();
////         m_hover_mesh = nullptr;
////     }
//// }

//// void Hover_tool::select()
//// {
////     ERHE_PROFILE_FUNCTION
////
////     const auto viewport_window = m_viewport_windows->hover_window();
////     if (!viewport_window)
////     {
////         return;
////     }
////
////     const auto& hover = viewport_window->get_hover(Hover_entry::content_slot);
////     if (!hover.valid || !hover.mesh)
////     {
////         return;
////     }
////
////     // Update hover information
////     m_hover_mesh            = hover.mesh;
////     m_hover_primitive_index = hover.primitive;
////
////     if (m_enable_color_highlight)
////     {
////         // Store original primitive material
////         m_original_primitive_material = m_hover_mesh->mesh_data.primitives[m_hover_primitive_index].material;
////
////         // Copy material and mutate it with increased material emissive
////         *m_hover_material.get()    = *m_original_primitive_material.get();
////         m_hover_material->name     = "hover";
////         m_hover_material->emissive = m_original_primitive_material->emissive + m_hover_emissive;
////         m_hover_material->index    = m_hover_material_index;
////
////         // Replace primitive material with modified copy
////         m_hover_mesh->mesh_data.primitives[m_hover_primitive_index].material = m_hover_material;
////         log_materials->info(
////             "hover mesh {} primitive {} set to {} with index {}",
////             m_hover_mesh->name(),
////             m_hover_primitive_index,
////             m_hover_material->name,
////             m_hover_material->index
////         );
////     }
//// }

} // namespace editor
