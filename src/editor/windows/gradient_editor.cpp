#include "windows/gradient_editor.hpp"

#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <imgui_gradient/imgui_gradient.hpp>
#include <imgui/imgui.h>

namespace editor {

Gradient_editor::Gradient_editor(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Gradient Editor", "gradient_editor"}
    , m_gradient_widget{std::make_unique<ImGG::GradientWidget>()}
{
    //m_gradient_widget->gradient().interpolation_mode
}

Gradient_editor::~Gradient_editor()
{
}

void Gradient_editor::imgui()
{
    // ImGui::Button("+");

    ImGG::Settings settings{};

    m_gradient_widget->widget("Test", settings);
    ImGui::Dummy(ImVec2{0, 0});
    //ImGG::Gradient& gradient = m_gradient_widget->gradient();

    //ImGui::BeginTable("Colors", 2);
    //for (int i = 0; i < 10; ++i) {
    //    ImGui::PushID(i);
    //    float t = static_cast<float>(i) / 9.0f;
    //    ImGui::TableNextRow();
    //    ImGui::TableNextColumn();
    //    ImGui::Text("%f", t);
    //    ImGui::TableNextColumn();
    //    ImGG::RelativePosition position{t};
    //    ImGG::ColorRGBA value = gradient.at(position);
    //    ImGui::ColorEdit4("##", &value.x, ImGuiColorEditFlags_NoInputs);
    //    ImGui::PopID();
    //}
    //ImGui::EndTable();
}

}
