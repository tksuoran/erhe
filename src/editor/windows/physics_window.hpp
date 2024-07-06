#pragma once

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_physics/idebug_draw.hpp"

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor {

class Editor_context;
class Selection_tool;
class Tools;
class Viewport_windows;

class Physics_window
    : public erhe::imgui::Imgui_window
{
public:
    Physics_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context
    );

    // Implements Tool
    //// TODO
    //// void tool_render(const Render_context& context) override;

    // Implements Window
    void imgui() override;

    // Public API
    void viewport_toolbar(bool& hovered);

    class Debug_draw_parameters
    {
    public:
        bool enable           {false};
        bool wireframe        {false};
        bool aabb             {true};
        bool contact_points   {true};
        bool no_deactivation  {false}; // forcibly disables deactivation when enabled
        bool constraints      {true};
        bool constraint_limits{true};
        bool normals          {false};
        bool frames           {true};

        erhe::physics::IDebug_draw::Colors colors;
    };

    [[nodiscard]] auto get_debug_draw_parameters() -> Debug_draw_parameters;

private:
    Editor_context&       m_context;
    Debug_draw_parameters m_debug_draw;
};

} // namespace editor
