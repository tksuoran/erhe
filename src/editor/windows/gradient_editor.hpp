#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <imgui/imgui.h>

namespace erhe::imgui { class Imgui_windows; }
namespace ImGG { class GradientWidget; }

namespace editor {

class App_context;

class Gradient_editor : public erhe::imgui::Imgui_window
{
public:
    Gradient_editor(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows
    );
    ~Gradient_editor();

    // Implements Imgui_window
    void imgui() override;

private:
    std::unique_ptr<ImGG::GradientWidget> m_gradient_widget;
};

} // namespace editor
