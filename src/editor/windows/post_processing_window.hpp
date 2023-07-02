#pragma once

#include "erhe/imgui/imgui_window.hpp"

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor
{

class Editor_context;

// ImGui window for showing downsample steps for a Post_processing node
class Post_processing_window
    : public erhe::imgui::Imgui_window
{
public:
    Post_processing_window(
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
