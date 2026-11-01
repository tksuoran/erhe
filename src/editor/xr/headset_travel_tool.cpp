#include "xr/headset_travel_tool.hpp"

#include "app_context.hpp"
#include "graphics/icon_set.hpp"
#include "time.hpp"
#include "tools/tools.hpp"
#include "xr/headset_view.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_xr/headset.hpp"
#include "erhe_xr/xr_action.hpp"

#include <imgui/imgui.h>

#include <algorithm>

namespace editor {

Headset_travel_command::Headset_travel_command(erhe::commands::Commands& commands, Headset_travel_tool& tool)
    : Command{commands, "Headset_travel.travel"}
    , m_tool {tool}
{
}

auto Headset_travel_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    m_tool.set_trigger_value(input.variant.float_value);
    return true;
}

Headset_travel_tool::Headset_travel_tool(
    erhe::commands::Commands& commands,
    App_context&              app_context,
    Headset_view&             headset_view,
    Icon_set&                 icon_set,
    Tools&                    tools
)
    : Tool            {app_context, tools, Tool_flags::toolbox}
    , m_travel_command{commands, *this}
    , m_headset_view  {headset_view}
{
    ERHE_PROFILE_FUNCTION();

    set_base_priority(c_priority);
    set_description  ("Travel Tool");
    set_icon         (icon_set.custom_icons, icon_set.icons.vive_trigger);

    commands.register_command(&m_travel_command);
    erhe::xr::Headset*    headset  = headset_view.get_headset();
    erhe::xr::Xr_actions* xr_right = (headset != nullptr) ? headset->get_actions_right() : nullptr;
    if ((xr_right != nullptr) && (xr_right->trigger_value != nullptr)) {
        commands.bind_command_to_xr_float_action(&m_travel_command, xr_right->trigger_value);
    }
    m_travel_command.set_host(this);
}

void Headset_travel_tool::set_trigger_value(const float value)
{
    m_trigger_value = value;
}

void Headset_travel_tool::handle_priority_update(const int old_priority, const int new_priority)
{
    // A disabled tool's command sees no trigger release: drop the held value.
    if (new_priority < old_priority) {
        m_trigger_value = 0.0f;
    }
}

void Headset_travel_tool::update_fixed_step(const Time_context& time_context)
{
    constexpr float trigger_threshold = 0.05f;
    if (!is_enabled() || (m_trigger_value < trigger_threshold)) {
        return;
    }

    const Hover_entry* nearest = m_headset_view.get_nearest_hover(
        Hover_entry::content_bit | Hover_entry::grid_bit | Hover_entry::rendertarget_bit
    );
    if ((nearest == nullptr) || !nearest->valid || !nearest->position.has_value() || (nearest->slot == Hover_entry::rendertarget_slot)) {
        return;
    }
    const std::optional<glm::vec3> user_position = m_headset_view.get_user_floor_position_in_world();
    if (!user_position.has_value()) {
        return;
    }

    const glm::vec3 delta    = nearest->position.value() - user_position.value();
    const float     distance = glm::length(delta);
    constexpr float arrive_distance = 0.05f;
    if (distance < arrive_distance) {
        return;
    }
    const float speed = std::min(m_max_speed, m_approach_rate * distance) * std::min(m_trigger_value, 1.0f);
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
