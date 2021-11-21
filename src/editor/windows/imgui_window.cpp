#include "windows/imgui_window.hpp"

namespace editor {

Imgui_window::Imgui_window(const std::string_view title)
    : m_title{title}
{
}

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

}