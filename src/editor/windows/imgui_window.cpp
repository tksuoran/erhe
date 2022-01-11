#include "windows/imgui_window.hpp"

#include <imgui.h>

namespace editor {

Imgui_window::Imgui_window(const std::string_view title)
    : m_title{title}
{
}

Imgui_window::~Imgui_window() = default;

void Imgui_window::show()
{
    m_is_visible = true;
}

void Imgui_window::hide()
{
    m_is_visible = false;
}

void Imgui_window::toggle_visibility()
{
    m_is_visible = !m_is_visible;
}

auto Imgui_window::is_visibile() const -> bool
{
    return m_is_visible;
}

auto Imgui_window::title() const -> const std::string_view
{
    return m_title;
}

auto Imgui_window::begin() -> bool
{
    on_begin();

    bool keep_visible{true};
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    const bool not_collapsed = ImGui::Begin(
        title().data(),
        &keep_visible,
        flags
    );
    if (!keep_visible)
    {
        hide();
    }
    return not_collapsed;
}

void Imgui_window::end()
{
    on_end();
    ImGui::End();
}

void Imgui_window::on_begin()
{
}

void Imgui_window::on_end()
{
}

}
