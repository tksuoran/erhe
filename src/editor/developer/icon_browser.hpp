#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <imgui/imgui.h>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor {

class App_context;

class Icon_browser : public erhe::imgui::Imgui_window
{
public:
    Icon_browser(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context
    );

    // Implements Imgui_window
    void imgui() override;

private:
    App_context&    m_context;
    ImGuiTextFilter m_name_filter;
    int             m_selected_font{0};
};

}
