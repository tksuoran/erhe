#include "windows/gradient_editor.hpp"

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <imgui/imgui.h>

namespace editor {

Gradient_editor::Gradient_editor(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Gradient Editor", "gradient_editor"}
{
}

void Gradient_editor::imgui()
{
    ImGui::Button("+");
}

} // namespace editor
