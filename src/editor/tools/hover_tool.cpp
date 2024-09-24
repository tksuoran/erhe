// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "tools/hover_tool.hpp"

#include "editor_context.hpp"
#include "editor_message_bus.hpp"
#include "renderers/render_context.hpp"
#include "scene/node_physics.hpp"
#include "scene/viewport_scene_view.hpp"
#include "tools/grid.hpp"
#include "tools/tools.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_renderer/line_renderer.hpp"
#include "erhe_renderer/scoped_line_renderer.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_defer/defer.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <string>

namespace editor {

Hover_tool::Hover_tool(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_message_bus&          editor_message_bus,
    Tools&                       tools
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Hover Tool", "hover_tool"}
    , Tool                     {editor_context}
{
    set_flags      (Tool_flags::background | Tool_flags::toolbox);
    set_description("Hover Tool");
    tools.register_tool(this);

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            Tool::on_message(message);
        }
    );
}

void Hover_tool::imgui()
{
    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }

    const auto& hover        = scene_view->get_hover(Hover_entry::content_slot);
    const auto& tool         = scene_view->get_hover(Hover_entry::tool_slot);
    const auto& rendertarget = scene_view->get_hover(Hover_entry::rendertarget_slot);
    const auto& grid         = scene_view->get_hover(Hover_entry::grid_slot);
    const auto* nearest      = scene_view->get_nearest_hover(Hover_entry::all_bits);
    if (nearest == nullptr) {
        return;
    }
    ImGui::Checkbox("Show Snapped Grid Position", &m_show_snapped_grid_position);
    ImGui::Text("Nearest: %s",      nearest->valid ? nearest->get_name().c_str() : "");
    ImGui::Text("Content: %s",      (hover       .valid && hover       .mesh) ? hover       .mesh->get_name().c_str() : "");
    ImGui::Text("Tool: %s",         (tool        .valid && tool        .mesh) ? tool        .mesh->get_name().c_str() : "");
    ImGui::Text("Rendertarget: %s", (rendertarget.valid && rendertarget.mesh) ? rendertarget.mesh->get_name().c_str() : "");
    if (grid.valid && grid.position.has_value()) {
        const std::string text = fmt::format("Grid: {}", grid.position.value());
        ImGui::TextUnformatted(text.c_str());
    }

    if (nearest->valid) {
        {
            const std::string text = fmt::format("Nearest Primitive Index: {}", nearest->primitive_index);
            ImGui::TextUnformatted(text.c_str());
        }
        {
            const std::string text = fmt::format("Nearest triangle Id: {}", nearest->triangle_id);
            ImGui::TextUnformatted(text.c_str());
        }
        {
            const std::string text = fmt::format("Nearest polygon Id: {}", nearest->polygon_id);
            ImGui::TextUnformatted(text.c_str());
        }
        if (nearest->position.has_value()) {
            const std::string text = fmt::format("Nearest Position: {}", nearest->position.value());
            ImGui::TextUnformatted(text.c_str());
        }
        if (nearest->normal.has_value()) {
            const std::string text = fmt::format("Nearest Normal: {}", nearest->normal.value());
            ImGui::TextUnformatted(text.c_str());
        }
        if (nearest->uv.has_value()) {
            const std::string text = fmt::format("Nearest UV: {}", nearest->uv.value());
            ImGui::TextUnformatted(text.c_str());
        }
    }
    const auto origin    = scene_view->get_control_ray_origin_in_world();
    const auto direction = scene_view->get_control_ray_direction_in_world();
    if (origin.has_value()) {
        const std::string text = fmt::format("Ray Origin: {}", origin.value());
        ImGui::TextUnformatted(text.c_str());
    }
    if (direction.has_value()) {
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

    switch (slot) {
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

    switch (slot) {
        case Hover_entry::content_slot: return red;
        case Hover_entry::tool_slot: return blue;
        case Hover_entry::grid_slot: return green;
        default: return white;
    }
}

void Hover_tool::add_line(const std::string& line)
{
    m_text_lines.push_back(line);
}

void Hover_tool::tool_render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    ERHE_DEFER(
        m_text_lines.clear();
    );

    if (get_hover_scene_view() == nullptr) {
        return;
    }

    const auto* entry = context.scene_view.get_nearest_hover(
        Hover_entry::content_bit | Hover_entry::grid_bit
    );

    if ((entry == nullptr) || !entry->valid || !entry->position.has_value()) {
        return;
    }

    if (entry->normal.has_value()) {
        erhe::renderer::Scoped_line_renderer line_renderer = context.get_line_renderer(2, true, true);
        const auto p0 = entry->position.value();
        const auto p1 = entry->position.value() + entry->normal.value();
        line_renderer.set_thickness(10.0f);
        line_renderer.add_lines(
            get_line_color_from_slot(entry->slot),
            {{ glm::vec3{p0}, glm::vec3{p1} }}
        );
        if (m_show_snapped_grid_position && entry->grid) {
            const auto sp0 = entry->grid->snap_world_position(p0);
            const auto sp1 = sp0 + entry->normal.value();
            line_renderer.add_lines(
                glm::vec4{1.0f, 1.0f, 0.0f, 1.0},
                {{ glm::vec3{sp0}, glm::vec3{sp1} }}
            );
        }
    }

    if (context.viewport_scene_view == nullptr) {
        return;
    }

    const auto position_in_viewport_opt = context.viewport_scene_view->project_to_viewport(entry->position.value());
    if (!position_in_viewport_opt.has_value()) {
        return;
    }

    //constexpr uint32_t red   = 0xff0000ffu;
    //constexpr uint32_t blue  = 0xffff0000u;
    //constexpr uint32_t white = 0xffffffffu;

    const uint32_t text_color = get_text_color_from_slot(entry->slot);

    const auto position_in_viewport = position_in_viewport_opt.value();
    glm::vec3 position_at_fixed_depth{
        position_in_viewport.x + 50.0f,
        position_in_viewport.y,
        -0.5f
    };

    add_line(entry->get_name());

#if 0
    add_line(fmt::format("Position in world: {}", entry->position.value()));
#endif

    const glm::vec3 position_at_fixed_depth_line_3{
        position_in_viewport.x + 50.0f,
        position_in_viewport.y + 16.0f * 2,
        -0.5f
    };
#if 0
    if (entry->mesh != nullptr) {
        const auto* node = entry->mesh->get_node();
        const glm::vec3 node_position_in_world = glm::vec3{node->position_in_world()};
        add_line(fmt::format("Node position in world: {}", node_position_in_world));
        if (entry->position.has_value()) {
            const glm::vec3 local_position = node->transform_point_from_world_to_local(entry->position.value());
            add_line(fmt::format("Position in {}: {}", entry->mesh->get_name(), local_position));
        }
    } else if (entry->grid != nullptr) {
        const glm::vec3 local_position = glm::vec3{
            entry->grid->grid_from_world() * glm::vec4{entry->position.value(), 1.0f}
        };
        add_line(fmt::format("Position in {}: {}", entry->grid->get_name(), local_position));
    }
#endif

    for (const auto& text_line : m_text_lines) {
        m_context.text_renderer->print(
            position_at_fixed_depth,
            text_color,
            text_line.c_str()
        );
        position_at_fixed_depth.y += 16.0f;
    }
}

} // namespace editor
