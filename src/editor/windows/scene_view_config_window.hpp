#pragma once

#include "erhe_imgui/imgui_window.hpp"

//namespace erhe::imgui { class Imgui_windows; }

namespace editor {

class App_context;
class Scene_view;

class Scene_view_config_window : public erhe::imgui::Imgui_window
{
public:
    Scene_view_config_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context
    );

    // Implements Imgui_window 
    void imgui() override;

    // Public API
    void set_scene_view(Scene_view* scene_view);

private:
    App_context&  m_context;
    Scene_view*   m_scene_view{nullptr};
};

}
