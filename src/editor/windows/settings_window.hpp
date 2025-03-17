#pragma once

#include "erhe_imgui/imgui_window.hpp"
#include "windows/property_editor.hpp"

#include <vector>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor {

class Editor_context;

class Settings_window : public erhe::imgui::Imgui_window, public Property_editor
{
public:
    Settings_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context
    );

    // Implements Imgui_window
    void imgui() override;

private:
    void rasterize_icons();

    Editor_context&          m_context;
    int                      m_msaa_sample_count_entry_index{0};
    std::vector<const char*> m_graphics_preset_names;
    int                      m_graphics_preset_index{0};
    int                      m_shadow_resolution_index{0};
};

} // namespace editor
