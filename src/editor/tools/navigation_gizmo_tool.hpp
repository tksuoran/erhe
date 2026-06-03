#pragma once

#include "tools/tool.hpp"
#include "ImViewGuizmo.h"
#include "app_message.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::commands {
    class Commands;
    struct Input_arguments;
}

namespace editor {

class App_context;
class App_message_bus;
class Tools;
class Viewport_scene_view;

// Drives the navigation gizmo (ImViewGuizmo) through erhe::commands so it can never run
// at the same time as another viewport mouse-drag. Bound to left mouse drag with
// call_on_button_down_without_motion = true, so a press over the gizmo is consumed before
// any other tool can react. A drag controls the camera (orbit/zoom/pan); a click on an
// axis handle (no motion) snaps the view to that axis from on_inactive().
class Navigation_gizmo_drag_command : public erhe::commands::Command
{
public:
    Navigation_gizmo_drag_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready          () override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;
    void on_inactive        () override;

private:
    App_context& m_context;
};

class Navigation_gizmo_tool : public Tool
{
public:
    // Above every viewport drag tool (fly camera is the highest at 5) so the gizmo wins
    // command arbitration over its own screen area and consumes the press first.
    static constexpr int c_priority{10};

    Navigation_gizmo_tool(
        erhe::commands::Commands& commands,
        App_context&              context,
        App_message_bus&          app_message_bus,
        Tools&                    tools
    );

    // API for Navigation_gizmo_drag_command
    auto on_drag_ready() -> bool;
    void on_drag_begin();
    auto on_drag      (glm::vec2 relative) -> bool;
    void on_drag_end  ();

private:
    // The drag target is the hovered viewport. It is re-fetched each call (like the other
    // viewport tools) rather than pinned: once this command is the active mouse command the
    // pointer capture keeps the originating viewport hovered for the whole drag.
    [[nodiscard]] auto get_target_scene_view() -> Viewport_scene_view*;

    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    Navigation_gizmo_drag_command                            m_drag_command;
    ImViewGuizmo::Region                                     m_drag_region {ImViewGuizmo::Region::none};
    int                                                      m_drag_axis   {-1};
    // Net pointer displacement since the press. The held-button tick calls on_drag() with a
    // zero delta every frame, so a click-vs-drag test must measure movement, not call count:
    // an axis-handle click snaps only while this stays below the drag threshold.
    glm::vec2                                                m_drag_offset {0.0f, 0.0f};
};

}
