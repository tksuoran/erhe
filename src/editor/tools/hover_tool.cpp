// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "tools/hover_tool.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "renderers/render_context.hpp"
#include "scene/material_library.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/grid.hpp"
#include "tools/grid_tool.hpp"
#include "tools/tools.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/physics/irigid_body.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <string>

namespace editor
{

Hover_tool* g_hover_tool{nullptr};

Hover_tool::Hover_tool()
    : erhe::application::Imgui_window{c_title}
    , erhe::components::Component{c_type_name}
{
}

Hover_tool::~Hover_tool() noexcept
{
    ERHE_VERIFY(g_hover_tool == this);
    g_hover_tool = nullptr;
}

void Hover_tool::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
    require<Editor_message_bus>();
    require<Tools             >();
}

void Hover_tool::initialize_component()
{
    ERHE_PROFILE_FUNCTION
    ERHE_VERIFY(g_hover_tool == nullptr);

    set_flags(Tool_flags::background);

    erhe::application::g_imgui_windows->register_imgui_window(this, "hover");

    set_description(c_title);
    set_flags      (Tool_flags::toolbox);
    g_tools->register_tool(this);

    g_editor_message_bus->add_receiver(
        [&](Editor_message& message)
        {
            Tool::on_message(message);
        }
    );

    g_hover_tool = this;
}

void Hover_tool::imgui()
{
    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr)
    {
        return;
    }

    const auto& hover        = scene_view->get_hover(Hover_entry::content_slot);
    const auto& tool         = scene_view->get_hover(Hover_entry::tool_slot);
    const auto& rendertarget = scene_view->get_hover(Hover_entry::rendertarget_slot);
    const auto& grid         = scene_view->get_hover(Hover_entry::grid_slot);
    const auto& nearest      = scene_view->get_nearest_hover(Hover_entry::all_bits);
    ImGui::Checkbox("Show Snapped Grid Position", &m_show_snapped_grid_position);
    ImGui::Text("Nearest: %s",      nearest.valid ? nearest.get_name().c_str() : "");
    ImGui::Text("Content: %s",      (hover       .valid && hover       .mesh) ? hover       .mesh->get_name().c_str() : "");
    ImGui::Text("Tool: %s",         (tool        .valid && tool        .mesh) ? tool        .mesh->get_name().c_str() : "");
    ImGui::Text("Rendertarget: %s", (rendertarget.valid && rendertarget.mesh) ? rendertarget.mesh->get_name().c_str() : "");
    if (grid.valid && grid.position.has_value())
    {
        const std::string text = fmt::format("Grid: {}", grid.position.value());
        ImGui::TextUnformatted(text.c_str());
    }

    if (nearest.valid)
    {
        {
            const std::string text = fmt::format("Nearest Primitive: {}", nearest.primitive);
            ImGui::TextUnformatted(text.c_str());
        }
        {
            const std::string text = fmt::format("Nearest local_index: {}", nearest.local_index);
            ImGui::TextUnformatted(text.c_str());
        }
        if (nearest.position.has_value())
        {
            const std::string text = fmt::format("Nearest Position: {}", nearest.position.value());
            ImGui::TextUnformatted(text.c_str());
        }
        if (nearest.normal.has_value())
        {
            const std::string text = fmt::format("Nearest Normal: {}", nearest.normal.value());
            ImGui::TextUnformatted(text.c_str());
        }
        if (nearest.uv.has_value())
        {
            const std::string text = fmt::format("Nearest UV: {}", nearest.uv.value());
            ImGui::TextUnformatted(text.c_str());
        }
    }
    const auto origin    = scene_view->get_control_ray_origin_in_world();
    const auto direction = scene_view->get_control_ray_direction_in_world();
    if (origin.has_value())
    {
        const std::string text = fmt::format("Ray Origin: {}", origin.value());
        ImGui::TextUnformatted(text.c_str());
    }
    if (direction.has_value())
    {
        const std::string text = fmt::format("Ray Direction: {}", direction.value());
        ImGui::TextUnformatted(text.c_str());
    }
}

[[nodiscard]] auto get_text_color_from_slot(std::size_t slot)
{
    constexpr uint32_t red  {0xff0000ffu};
    constexpr uint32_t green{0xff00ff00u};
    constexpr uint32_t blue {0xffff0000u};
    constexpr uint32_t white{0xffffffffu};

    switch (slot)
    {
        case Hover_entry::content_slot: return white;
        case Hover_entry::tool_slot:    return blue;
        case Hover_entry::grid_slot:    return green;
        default: return red;
    }
}

[[nodiscard]] auto get_line_color_from_slot(std::size_t slot)
{
    constexpr glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
    constexpr glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
    constexpr glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};
    constexpr glm::vec4 white{1.0f, 1.0f, 1.0f, 1.0f};

    switch (slot)
    {
        case Hover_entry::content_slot: return red;
        case Hover_entry::tool_slot: return blue;
        case Hover_entry::grid_slot: return green;
        default: return white;
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

    const auto& entry = context.scene_view->get_nearest_hover(Hover_entry::content_bit | Hover_entry::grid_bit);

    if (
        !entry.valid ||
        !entry.position.has_value()
    )
    {
        return;
    }

    auto& line_renderer = *erhe::application::g_line_renderer_set->hidden.at(2).get();

    if (entry.normal.has_value())
    {
        const auto p0 = entry.position.value();
        const auto p1 = entry.position.value() + entry.normal.value();
        line_renderer.set_thickness(10.0f);
        line_renderer.add_lines(
            get_line_color_from_slot(entry.slot),
            {
                {
                    glm::vec3{p0},
                    glm::vec3{p1}
                }
            }
        );
        if (m_show_snapped_grid_position && entry.grid)
        {
            const auto sp0 = entry.grid->snap_world_position(p0);
            const auto sp1 = sp0 + entry.normal.value();
            line_renderer.add_lines(
                glm::vec4{1.0f, 1.0f, 0.0f, 1.0},
                {
                    {
                        glm::vec3{sp0},
                        glm::vec3{sp1}
                    }
                }
            );
        }
    }

    if (context.viewport_window == nullptr)
    {
        return;
    }

    const auto position_in_viewport_opt = context.viewport_window->project_to_viewport(entry.position.value());
    if (!position_in_viewport_opt.has_value())
    {
        return;
    }

    //constexpr uint32_t red   = 0xff0000ffu;
    //constexpr uint32_t blue  = 0xffff0000u;
    //constexpr uint32_t white = 0xffffffffu;

    const uint32_t text_color = get_text_color_from_slot(entry.slot);

    const auto position_in_viewport = position_in_viewport_opt.value();
    const glm::vec3 position_at_fixed_depth{
        position_in_viewport.x + 50.0f,
        position_in_viewport.y,
        -0.5f
    };

    const std::string text = entry.get_name();

    erhe::application::g_text_renderer->print(
        position_at_fixed_depth,
        text_color,
        text
    );

    const glm::vec3 position_at_fixed_depth_line_2{
        position_in_viewport.x + 50.0f,
        position_in_viewport.y + 16.0f,
        -0.5f
    };
    const std::string text_line_2 = fmt::format(
        "Position in world: {}",
        entry.position.value()
    );
    erhe::application::g_text_renderer->print(
        position_at_fixed_depth_line_2,
        text_color,
        text_line_2
    );

    const glm::vec3 position_at_fixed_depth_line_3{
        position_in_viewport.x + 50.0f,
        position_in_viewport.y + 16.0f * 2,
        -0.5f
    };
    if (entry.mesh)
    {
        const auto* node = entry.mesh->get_node();
        auto node_physics = get_node_physics(node);
        if (node_physics)
        {
            erhe::physics::IRigid_body* rigid_body = node_physics->rigid_body();
            if (rigid_body)
            {
                const glm::vec3 local_position = node->transform_point_from_world_to_local(entry.position.value());
                const std::string text_line_3 = fmt::format(
                    "Position in {}: {}",
                    entry.mesh->get_name(),
                    local_position
                );
                erhe::application::g_text_renderer->print(
                    position_at_fixed_depth_line_3,
                    text_color,
                    text_line_3.c_str()
                );
            }
        }
    }
    else if (entry.grid)
    {
        const glm::vec3 local_position = glm::vec3{
            entry.grid->grid_from_world() * glm::vec4{entry.position.value(), 1.0f}
        };
        const std::string text_line_3 = fmt::format(
            "Position in {}: {}",
            entry.grid->get_name(),
            local_position
        );
        erhe::application::g_text_renderer->print(
            position_at_fixed_depth_line_3,
            text_color,
            text_line_3.c_str() // erhe::physics::c_motion_mode_strings[motion_mode_index]
        );
    }
}

} // namespace editor
