#pragma once

#include "erhe_imgui/imgui_window.hpp"

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor {

class Editor_context;

class Clipboard_window
    : public erhe::imgui::Imgui_window
{
public:
    Clipboard_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor
    );

    // Implements Imgui_window
    void imgui() override;

private:
    Editor_context& m_context;
};

}
