#pragma once

#if defined(ERHE_XR_LIBRARY_OPENXR)

#include "tools/tool.hpp"

namespace editor {

class App_context;
class Headset_view;
class Icon_set;
class Time_context;
class Tools;

// Hotbar tool, the default priority tool under OpenXR: while the right
// trigger is pressed, the headset camera root moves towards the point the
// controller ray hits (content or grid), carrying the user's floor point
// under the head towards it. The speed grows with the distance to the
// point (capped) and scales with the analog trigger value. A ray that hits
// nothing flies along the ray at the trigger's share of the max speed. A
// ray that hits a rendertarget (Hud, Hotbar) moves nothing.
//
// The trigger value is read from the action state each fixed step, as
// Rendertarget_imgui_host reads it, not from input events: the trigger's
// float events go to the first command that consumes them, so an event-fed
// value can stop updating.
class Headset_travel_tool : public Tool
{
public:
    static constexpr int c_priority{2};

    Headset_travel_tool(
        App_context&  app_context,
        Headset_view& headset_view,
        Icon_set&     icon_set,
        Tools&        tools
    );

    // M2, once per fixed step (after Headset_view::update_fixed_step()).
    void update_fixed_step(const Time_context& time_context);

    // Implements Tool
    void tool_properties(erhe::imgui::Imgui_window& imgui_window) override;

private:
    // Why a pressed trigger did not move the camera root (logged once per
    // press, on change).
    enum class Blocked_reason : unsigned int
    {
        none = 0,
        not_priority_tool,
        no_controller_ray,
        rendertarget_hover,
        no_head_pose,
        arrived
    };
    void log_blocked(Blocked_reason reason);
    [[nodiscard]] static auto get_blocked_reason_text(Blocked_reason reason) -> const char*;

    Headset_view&  m_headset_view;
    float          m_approach_rate {1.5f};  // 1/s: speed = rate * distance ...
    float          m_max_speed     {12.0f}; // m/s: ... capped here
    bool           m_pressed       {false};
    Blocked_reason m_logged_reason {Blocked_reason::none};
};

}

#endif
