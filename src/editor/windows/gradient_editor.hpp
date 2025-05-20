#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <imgui/imgui.h>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor {

class Editor_context;

class Gradient_editor : public erhe::imgui::Imgui_window
{
public:
    Gradient_editor(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows
    );

    // Implements Imgui_window
    void imgui() override;
};

} // namespace editor
