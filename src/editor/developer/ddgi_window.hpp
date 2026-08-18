#pragma once

#include "erhe_imgui/imgui_window.hpp"

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace editor {

class App_context;

// Developer window for dynamic diffuse global illumination
// (doc/ddgi-plan.md): the enable toggle, the fitted probe grid's stats, and
// previews of the probe atlases. The knobs themselves live in the editor
// settings (Settings window, DDGI section) - this window is the diagnostic
// view of what the renderer made of them.
class Ddgi_window : public erhe::imgui::Imgui_window
{
public:
    Ddgi_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context
    );

    // Implements Imgui_window
    void imgui() override;

private:
    App_context& m_context;
};

} // namespace editor
