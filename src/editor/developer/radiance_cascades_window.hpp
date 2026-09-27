#pragma once

#include "erhe_imgui/imgui_window.hpp"

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace editor {

class App_context;

// Developer window for radiance cascades (doc/editor/radiance_cascades.md):
// the indirect diffuse source selection and the fitted cascade layout - per
// cascade probe counts, spacing, octahedral tile size, radiance interval,
// texels and atlas memory, plus totals. The knobs themselves live in the
// editor settings (Settings window, Radiance Cascades section).
class Radiance_cascades_window : public erhe::imgui::Imgui_window
{
public:
    Radiance_cascades_window(
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
