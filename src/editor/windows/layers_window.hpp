#pragma once

#include "erhe/imgui/imgui_window.hpp"

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor
{

class Editor_context;

class Layers_window
    : public erhe::imgui::Imgui_window
{
public:
    Layers_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context
    );

    // Implements Imgui_window
    void imgui() override;

private:
    Editor_context& m_context;
};

} // namespace editor
