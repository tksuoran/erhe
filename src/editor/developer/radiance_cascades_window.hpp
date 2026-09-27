#pragma once

#include "erhe_imgui/imgui_window.hpp"

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace editor {

class App_context;
enum class Rc_preview_channel : unsigned int;
enum class Rc_preview_source : unsigned int;

// Developer window for radiance cascades (doc/editor/radiance_cascades.md):
// the indirect diffuse source selection and the fitted cascade layout - per
// cascade probe counts, spacing, octahedral tile size, radiance interval,
// texels and atlas memory, plus totals; the trace and merge GPU times, the
// debug cascade mask (Radiance_cascades_config::debug_cascade_mask) and an
// atlas preview of a chosen cascade, atlas (raw / merged) and channel. The
// other knobs live in the editor settings (Settings window, Radiance
// Cascades section).
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
    App_context&       m_context;
    // Atlas preview selection (window state, not a setting).
    int                m_preview_cascade       {0};
    Rc_preview_source  m_preview_source        {0}; // Rc_preview_source::raw
    Rc_preview_channel m_preview_channel       {0}; // Rc_preview_channel::radiance
    float              m_preview_radiance_scale{1.0f};
};

} // namespace editor
