#pragma once

#include "erhe_imgui/imgui_window.hpp"

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace editor {

class App_context;

// Developer window for the GPU ray tracing renderer (issue #233): toggles
// Ray_trace_renderer and shows its N.V-shaded primary-ray output texture.
class Ray_trace_window : public erhe::imgui::Imgui_window
{
public:
    Ray_trace_window(
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
