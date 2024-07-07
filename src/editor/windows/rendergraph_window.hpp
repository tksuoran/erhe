#pragma once

#include "erhe_imgui/imgui_window.hpp"

namespace erhe::imgui {
    class Imgui_windows;
}
namespace ImNodes::Ez {
    struct Context;
}

namespace editor {

class Editor_context;

class Rendergraph_window : public erhe::imgui::Imgui_window
{
public:
    Rendergraph_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context
    );
    ~Rendergraph_window() noexcept override;

    // Implements Imgui_window
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

private:
    Editor_context&       m_context;
    float                 m_image_size{100.0f};
    float                 m_curve_strength{10.0f};
    ImNodes::Ez::Context* m_imnodes_context{nullptr};
};

} // namespace editor
