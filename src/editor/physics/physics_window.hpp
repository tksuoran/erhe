#pragma once

#include "erhe_imgui/imgui_window.hpp"

namespace erhe::imgui { class Imgui_windows; }

namespace editor {

class App_context;
class Selection_tool;
class Tools;
class Scene_views;

class Physics_window : public erhe::imgui::Imgui_window
{
public:
    Physics_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context
    );

    // Implements Tool
    //// TODO
    //// void tool_render(const Render_context& context) override;

    // Implements Window
    void imgui() override;

    // Public API
    void viewport_toolbar(bool& hovered);

private:
    App_context& m_context;
};

}
