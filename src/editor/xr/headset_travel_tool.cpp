#include "xr/headset_travel_tool.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "graphics/icon_set.hpp"
#include "time.hpp"
#include "tools/hud.hpp"
#include "tools/tools.hpp"
#include "xr/headset_view.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_xr/headset.hpp"
#include "erhe_xr/xr_action.hpp"

#include <imgui/imgui.h>

#include <algorithm>

namespace editor {

Headset_travel_tool::Headset_travel_tool(
    App_context&  app_context,
    Headset_view& headset_view,
    Icon_set&     icon_set,
    Tools&        tools
)
    : Tool          {app_context, tools, Tool_flags::toolbox}
    , m_headset_view{headset_view}
{
    ERHE_PROFILE_FUNCTION();

    set_base_priority(c_priority);
    set_description  ("Travel Tool");
    set_icon         (icon_set.custom_icons, icon_set.icons.vive_trigger);
}

auto Headset_travel_tool::get_blocked_reason_text(const Blocked_reason reason) -> const char*
{
    switch (reason) {
        case Blocked_reason::not_priority_tool:  return "the Travel tool is not the active tool";
        case Blocked_reason::no_controller_ray:  return "the controller ray is not located";
        case Blocked_reason::rendertarget_hover: return "the controller ray hits a rendertarget (Hud / Hotbar)";
        case Blocked_reason::no_head_pose:       return "the head pose is not located";
        case Blocked_reason::arrived:            return "arrived at the ray hit";
        default:                                 return "none";
    }
}

void Headset_travel_tool::log_blocked(const Blocked_reason reason)
{
    if (reason == m_logged_reason) {
        return;
    }
    m_logged_reason = reason;
    if (reason != Blocked_reason::none) {
        const bool hud_dragging = (m_context.hud != nullptr) && m_context.hud->is_dragging();
        log_xr->info("Travel tool: trigger pressed but not moving: {} (hud dragging {})", get_blocked_reason_text(reason), hud_dragging);
    }
}

void Headset_travel_tool::update_fixed_step(const Time_context& time_context)
{
    erhe::xr::Headset*    headset       = m_headset_view.get_headset();
    erhe::xr::Xr_actions* actions_right = (headset != nullptr) ? headset->get_actions_right() : nullptr;
    erhe::xr::Xr_action_float* trigger  = (actions_right != nullptr) ? actions_right->trigger_value : nullptr;
    const float trigger_value =
        ((trigger != nullptr) && (trigger->state.isActive == XR_TRUE))
            ? trigger->state.currentState
            : 0.0f;

    // Press / release hysteresis for the diagnostics.
    if (!m_pressed && (trigger_value >= 0.1f)) {
        m_pressed       = true;
        m_logged_reason = Blocked_reason::none;
    } else if (m_pressed && (trigger_value <= 0.05f)) {
        m_pressed = false;
    }
    if (!m_pressed) {
        return;
    }

    if (!is_enabled()) {
        log_blocked(Blocked_reason::not_priority_tool);
        return;
    }
    const Hover_entry* nearest = m_headset_view.get_nearest_hover(
        Hover_entry::content_bit | Hover_entry::grid_bit | Hover_entry::rendertarget_bit
    );
    const bool has_hit = (nearest != nullptr) && nearest->valid && nearest->position.has_value();
    if (has_hit && (nearest->slot == Hover_entry::rendertarget_slot)) {
        log_blocked(Blocked_reason::rendertarget_hover);
        return;
    }
    if (!has_hit) {
        // No content or grid under the ray (nothing there, or scene object
        // hits are off): fly along the ray at the trigger's share of max speed.
        const std::optional<glm::vec3> direction = m_headset_view.get_control_ray_direction_in_world();
        if (!direction.has_value()) {
            log_blocked(Blocked_reason::no_controller_ray);
            return;
        }
        m_logged_reason = Blocked_reason::none;
        const float speed = m_max_speed * std::min(trigger_value, 1.0f);
        m_headset_view.translate_camera_root((speed * time_context.simulation_dt_s) * direction.value());
        return;
    }
    const std::optional<glm::vec3> user_position = m_headset_view.get_user_floor_position_in_world();
    if (!user_position.has_value()) {
        log_blocked(Blocked_reason::no_head_pose);
        return;
    }

    const glm::vec3 delta    = nearest->position.value() - user_position.value();
    const float     distance = glm::length(delta);
    constexpr float arrive_distance = 0.05f;
    if (distance < arrive_distance) {
        log_blocked(Blocked_reason::arrived);
        return;
    }
    m_logged_reason = Blocked_reason::none;
    const float speed = std::min(m_max_speed, m_approach_rate * distance) * std::min(trigger_value, 1.0f);
    const float step  = std::min(distance, speed * time_context.simulation_dt_s);
    m_headset_view.translate_camera_root((step / distance) * delta);
}

void Headset_travel_tool::tool_properties(erhe::imgui::Imgui_window& imgui_window)
{
    static_cast<void>(imgui_window);
    ImGui::SliderFloat("Approach rate", &m_approach_rate, 0.1f, 5.0f,  "%.2f 1/s");
    ImGui::SliderFloat("Max speed",     &m_max_speed,     0.5f, 50.0f, "%.1f m/s");
}

}
