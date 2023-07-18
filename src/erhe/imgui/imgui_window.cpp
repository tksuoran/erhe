#include "erhe/imgui/imgui_window.hpp"
#include "erhe/imgui/imgui_renderer.hpp"
#include "erhe/imgui/imgui_viewport.hpp"
#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/toolkit/profile.hpp"

#include <imgui.h>

namespace erhe::imgui {

Imgui_window::Imgui_window(
    Imgui_renderer&        imgui_renderer,
    Imgui_windows&         imgui_windows,
    const std::string_view title,
    const char*            ini_label
)
    : m_imgui_renderer{imgui_renderer}
    , m_title         {title}
    , m_ini_label     {ini_label}
{
    imgui_windows.register_imgui_window(this);
}

Imgui_window::~Imgui_window() noexcept
{
    // TODO: unregister_imgui_window
}

void Imgui_window::image(
    const std::shared_ptr<erhe::graphics::Texture>& texture,
    const int                                       width,
    const int                                       height
)
{
    m_imgui_renderer.image(
        texture,
        width,
        height,
        glm::vec2{0.0f, 1.0f},
        glm::vec2{1.0f, 0.0f},
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        true
    );
}

auto Imgui_window::get_viewport() const -> Imgui_viewport*
{
    return m_imgui_viewport;
}

void Imgui_window::set_viewport(Imgui_viewport* imgui_viewport)
{
    m_imgui_viewport = imgui_viewport;
}

void Imgui_window::set_visibility(const bool visible)
{
    m_is_visible = visible;
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

auto Imgui_window::show_in_menu() const -> bool
{
    return m_show_in_menu;
}

auto Imgui_window::is_visible() const -> bool
{
    return m_is_visible;
}

auto Imgui_window::is_hovered() const -> bool
{
    return m_is_hovered;
}

auto Imgui_window::ini_label() const -> const char*
{
    return m_ini_label;
}

auto Imgui_window::title() const -> const std::string_view
{
    return m_title;
}

[[nodiscard]] auto Imgui_window::get_scale_value() const -> float
{
    return (m_imgui_viewport != nullptr) ? m_imgui_viewport->get_scale_value() : 0.0f;
}

void Imgui_window::set_min_size(const float min_width, const float min_height)
{
    m_min_size[0] = min_width;
    m_min_size[1] = min_height;
}

auto Imgui_window::begin() -> bool
{
    on_begin();
    bool keep_visible{true};
    ImGui::SetNextWindowSizeConstraints(
        ImVec2{m_min_size[0], m_min_size[1]},
        ImVec2{m_max_size[0], m_max_size[1]}
    );
    const bool not_collapsed = ImGui::Begin(
        m_title.c_str(),
        &keep_visible,
        flags()
    );
    if (!keep_visible) {
        hide();
    }
    return not_collapsed;
}

void Imgui_window::end()
{
    on_end();
    m_is_hovered = ImGui::IsWindowHovered();
    ImGui::End();
}

auto Imgui_window::flags() -> ImGuiWindowFlags
{
    return 0; //ImGuiWindowFlags_NoCollapse;
}

auto Imgui_window::has_toolbar() const -> bool
{
    return false;
}

void Imgui_window::toolbar(bool& hovered)
{
    static_cast<void>(hovered);
}

auto Imgui_window::want_keyboard_events() const -> bool
{
    return false;
}

auto Imgui_window::want_mouse_events() const -> bool
{
    return false;
}

void Imgui_window::on_begin()
{
}

void Imgui_window::on_end()
{
}

void Imgui_window::hidden()
{
}


} // namespace erhe::imgui
