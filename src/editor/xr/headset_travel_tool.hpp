#pragma once

#if defined(ERHE_XR_LIBRARY_OPENXR)

#include "tools/tool.hpp"

#include "erhe_commands/command.hpp"

namespace erhe::commands { class Commands; }

namespace editor {

class App_context;
class Headset_travel_tool;
class Headset_view;
class Icon_set;
class Time_context;
class Tools;

// Right trigger value while the travel tool is the priority tool.
class Headset_travel_command : public erhe::commands::Command
{
public:
    Headset_travel_command(erhe::commands::Commands& commands, Headset_travel_tool& tool);

    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Headset_travel_tool& m_tool;
};

// Hotbar tool, the default priority tool under OpenXR: while the right
// trigger is pressed, the headset camera root moves towards the point the
// controller ray hits (content or grid), carrying the user's floor point
// under the head towards it. The speed grows with the distance to the
// point (capped) and scales with the analog trigger value. A ray that hits
// a rendertarget (Hud, Hotbar) moves nothing.
class Headset_travel_tool : public Tool
{
public:
    static constexpr int c_priority{2};

    Headset_travel_tool(
        erhe::commands::Commands& commands,
        App_context&              app_context,
        Headset_view&             headset_view,
        Icon_set&                 icon_set,
        Tools&                    tools
    );

    // M2, once per fixed step (after Headset_view::update_fixed_step()).
    void update_fixed_step(const Time_context& time_context);

    void set_trigger_value(float value);

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;
    void tool_properties       (erhe::imgui::Imgui_window& imgui_window) override;

private:
    Headset_travel_command m_travel_command;
    Headset_view&          m_headset_view;
    float                  m_trigger_value    {0.0f};
    float                  m_approach_rate    {1.5f};  // 1/s: speed = rate * distance ...
    float                  m_max_speed        {12.0f}; // m/s: ... capped here
};

}

#endif
