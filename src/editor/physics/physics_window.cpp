#include "physics/physics_window.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>

namespace editor
{

Physics_window::Physics_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, App_context& app_context)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Physics", "physics"}
    , m_context                {app_context}
{
}

void Physics_window::viewport_toolbar(bool& hovered)
{
    bool physics_enabled = m_context.editor_settings->physics.dynamic_enable;

    ImGui::SameLine();
    const bool pressed = erhe::imgui::make_button("P", (physics_enabled) ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal);
    if (ImGui::IsItemHovered()) {
        hovered = true;
        ImGui::SetTooltip(
            physics_enabled ? "Toggle physics on -> off" : "Toggle physics off -> on"
        );
    };

    if (pressed && m_context.editor_settings->physics.static_enable) {
        m_context.editor_settings->physics.dynamic_enable = !m_context.editor_settings->physics.dynamic_enable;
        m_context.app_settings->settings_store().touch();
    }
}

void Physics_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    if (!m_context.editor_settings->physics.static_enable) {
        ImGui::BeginDisabled();
    }
    bool changed = false;
    changed |= ImGui::Checkbox("Physics enabled", &m_context.editor_settings->physics.dynamic_enable);
    changed |= ImGui::Checkbox("Debug draw", &m_context.editor_settings->physics.debug_draw);
    if (ImGui::CollapsingHeader("Wind")) {
        Physics_config& physics = m_context.editor_settings->physics;
        changed |= ImGui::Checkbox   ("Enable##wind", &physics.wind_enable);
        changed |= ImGui::InputFloat3("Direction",    &physics.wind_direction.x);
        changed |= ImGui::SliderFloat("Speed",           &physics.wind_speed,           0.0f, 30.0f, "%.1f m/s");
        changed |= ImGui::SliderFloat("Gust Amplitude",  &physics.wind_gust_amplitude,  0.0f, 15.0f, "%.1f m/s");
        changed |= ImGui::SliderFloat("Gust Frequency",  &physics.wind_gust_frequency,  0.0f,  3.0f, "%.2f Hz");
        changed |= ImGui::SliderFloat("Turbulence",      &physics.wind_turbulence,      0.0f,  1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Gust Wavelength", &physics.wind_wavelength,      0.1f, 50.0f, "%.1f m");
    }
    if (changed) {
        m_context.app_settings->settings_store().touch();
    }
    if (!m_context.editor_settings->physics.static_enable) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("erhe.json has physics static_enable = false");
        }
    }

    // Trigger (sensor) overlap events of the hovered / single scene,
    // collected by Scene_root from the physics world callbacks.
    {
        const auto viewport_scene_view = m_context.scene_views->last_scene_view();
        const std::shared_ptr<Scene_root> scene_root = viewport_scene_view
            ? viewport_scene_view->get_scene_root()
            : m_context.app_scenes->get_single_scene_root();
        if (scene_root && ImGui::CollapsingHeader("Trigger Events")) {
            if (ImGui::Button("Clear")) {
                scene_root->clear_trigger_event_log();
            }
            ImGui::SameLine();
            ImGui::Text("%llu events", static_cast<unsigned long long>(scene_root->get_trigger_event_count()));
            ImGui::BeginChild("##trigger_events", ImVec2{0.0f, 200.0f}, ImGuiChildFlags_Borders);
            const std::deque<std::string>& trigger_event_log = scene_root->get_trigger_event_log();
            for (const std::string& line : trigger_event_log) {
                ImGui::TextUnformatted(line.c_str());
            }
            // Follow the newest entry while new events arrive.
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
        }
    }

    if (!m_context.developer_mode) {
        return;
    }

    const auto& scene_roots = m_context.app_scenes->get_scene_roots();
    for (const auto& scene_root : scene_roots) {
        if (!ImGui::TreeNodeEx(scene_root->get_name().c_str())) {
            continue;
        }

        // No physics world is created when physics is disabled (static_enable =
        // false); the scene_root then has no world to describe.
        if (!scene_root->has_physics_world()) {
            ImGui::TextUnformatted("(physics disabled - no world)");
            ImGui::TreePop();
            continue;
        }

        const auto& physics_world = scene_root->get_physics_world();
        const auto& debug_info = physics_world.describe();
        for (const auto& line : debug_info) {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::TreePop();
    }

    const auto viewport_scene_view = m_context.scene_views->last_scene_view();
    const auto scene_root = viewport_scene_view
        ? viewport_scene_view->get_scene_root()
        // No viewport hovered yet: when exactly one scene exists, use it.
        : m_context.app_scenes->get_single_scene_root();
    if (!scene_root) {
        return;
    }
    // Physics disabled (static_enable = false) -> no world to edit.
    if (!scene_root->has_physics_world()) {
        return;
    }

    auto& physics_world = scene_root->get_physics_world();
    const auto gravity = physics_world.get_gravity();
    {
        float floats[3] = { gravity.x, gravity.y, gravity.z };
        ImGui::InputFloat3("Gravity", floats);
        glm::vec3 updated_gravity{ floats[0], floats[1], floats[2] };
        if (updated_gravity != gravity) {
            physics_world.set_gravity(updated_gravity);
        }
    }
}

}
