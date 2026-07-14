// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_log.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace erhe::imgui {

Imgui_window::Imgui_window(
    Imgui_renderer&        imgui_renderer,
    Imgui_windows&         imgui_windows,
    const std::string_view title,
    const std::string_view ini_label,
    bool                   developer
)
    : m_imgui_renderer{imgui_renderer}
    , m_imgui_windows {imgui_windows}
    , m_title         {title}
    , m_developer     {developer}
    , m_ini_label     {ini_label}
{
    if (!ini_label.empty()) {
        m_is_visible = imgui_windows.get_persistent_window_open(ini_label);
    }

    imgui_windows.register_imgui_window(this);
}

Imgui_window::~Imgui_window() noexcept
{
    m_imgui_windows.unregister_imgui_window(this);
}

void Imgui_window::draw_image(
    const std::shared_ptr<erhe::graphics::Texture_reference>& texture_reference,
    const int                                                 width,
    const int                                                 height,
    const erhe::graphics::Filter                              filter,
    const erhe::graphics::Sampler_mipmap_mode                 mipmap_mode
)
{
    m_imgui_renderer.image(
        Draw_texture_parameters{
            .texture_reference = texture_reference,
            .width             = width,
            .height            = height,
            .uv0               = m_imgui_renderer.get_rtt_uv0(),
            .uv1               = m_imgui_renderer.get_rtt_uv1(),
            .filter            = filter,
            .mipmap_mode       = mipmap_mode,
            .debug_label       = "Imgui_window::draw_image()"
        }
    );
}

auto Imgui_window::get_imgui_host() const -> Imgui_host*
{
    return m_imgui_host;
}

void Imgui_window::set_imgui_host(Imgui_host* imgui_host)
{
    m_imgui_host = imgui_host;
}

void Imgui_window::set_window_visibility(const bool visible)
{
    if (m_is_visible == visible) {
        return;
    }
    m_is_visible = visible;
    if (!m_ini_label.empty()) {
        m_imgui_windows.save_window_state();
    }
}

void Imgui_window::show_window()
{
    set_window_visibility(true);
}

void Imgui_window::hide_window()
{
    set_window_visibility(false);
}

void Imgui_window::toggle_window_visibility()
{
    set_window_visibility(!m_is_visible);
}

auto Imgui_window::show_in_menu() const -> bool
{
    return m_show_in_menu;
}

auto Imgui_window::is_window_visible() const -> bool
{
    return m_is_visible;
}

auto Imgui_window::get_ini_label() const -> const std::string&
{
    return m_ini_label;
}

auto Imgui_window::get_title() const -> const std::string&
{
    return m_title;
}

auto Imgui_window::get_scale_value() const -> float
{
    return (m_imgui_host != nullptr) ? m_imgui_host->get_scale_value() : 0.0f;
}

void Imgui_window::set_min_size(const float min_width, const float min_height)
{
    m_min_size[0] = min_width;
    m_min_size[1] = min_height;
}

void Imgui_window::set_max_size(const float max_width, const float max_height)
{
    m_max_size[0] = max_width;
    m_max_size[1] = max_height;
}

void Imgui_window::set_show_in_menu(const bool show)
{
    m_show_in_menu = show;
}

void Imgui_window::request_window_focus()
{
    m_focus_requested = true;
    show_window();
}

void Imgui_window::set_initial_placement(
    const std::string_view dock_target_title,
    const float            fallback_width_ratio,
    const float            fallback_height_ratio
)
{
    m_has_initial_placement     = true;
    m_initial_dock_target_title = dock_target_title;
    m_initial_width_ratio       = fallback_width_ratio;
    m_initial_height_ratio      = fallback_height_ratio;
}

void Imgui_window::apply_initial_placement()
{
    if (!m_has_initial_placement) {
        return;
    }
    m_has_initial_placement = false;

    // ImGuiCond_FirstUseEver makes the placement apply only to a window ImGui
    // has no prior state for: it is disarmed when persisted ini settings exist
    // at ImGuiWindow creation, and consumed once a placement applies or the
    // window spends a hosted frame docked (the dock node position write
    // consumes the pos/size conds). This keeps the remembered layout intact
    // for a window recreated with the same identity (the "###" id suffix) -
    // e.g. the viewport window recreated when the last scene is closed stays
    // in its dock node instead of coming back floating.

    // Prefer docking (tabbing) into the target window's dock node, if it is
    // currently docked.
    if (!m_initial_dock_target_title.empty()) {
        const ImGuiWindow* target_window = ImGui::FindWindowByName(m_initial_dock_target_title.c_str());
        if ((target_window != nullptr) && (target_window->DockId != 0)) {
            ImGui::SetNextWindowDockID(target_window->DockId, ImGuiCond_FirstUseEver);
            return;
        }
    }

    // Floating fallback: center on the main viewport at the requested fraction
    // of its size.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 size{
        viewport->Size.x * m_initial_width_ratio,
        viewport->Size.y * m_initial_height_ratio
    };
    const ImVec2 pos{
        viewport->Pos.x + ((viewport->Size.x - size.x) * 0.5f),
        viewport->Pos.y + ((viewport->Size.y - size.y) * 0.5f)
    };
    ImGui::SetNextWindowPos (pos,  ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
}

auto Imgui_window::begin() -> bool
{
    ERHE_PROFILE_FUNCTION();

    on_begin();
    apply_initial_placement();
    if (m_focus_requested) {
        m_focus_requested = false;
        // Selects this window's dock tab and brings it to the front: Begin()
        // routes this through FocusWindow(), and the dock node tab bar applies
        // NavWindow focus back to its SelectedTabId.
        ImGui::SetNextWindowFocus();
    }
    bool keep_visible{true};
    ImGui::SetNextWindowSizeConstraints(
        ImVec2{m_min_size[0], m_min_size[1]},
        ImVec2{m_max_size[0], m_max_size[1]}
    );
    const bool not_collapsed = ImGui::Begin(m_title.c_str(), &keep_visible, flags());
    if (!keep_visible) {
        hide_window();
    }
    return not_collapsed;
}

void Imgui_window::end()
{
    ERHE_PROFILE_FUNCTION();

    on_end();
    const bool new_is_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    SPDLOG_LOGGER_TRACE(log_frame, "{}.end() is_hovered {} -> {}", m_title, m_is_hovered, new_is_hovered);
    m_is_hovered = new_is_hovered;
    ImGui::End();
}

auto Imgui_window::flags() -> ImGuiWindowFlags
{
    return 0; //ImGuiWindowFlags_NoCollapse;
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

auto Imgui_window::want_cursor_relative_hold() const -> bool
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

auto Imgui_window::show_in_developer_menu() const -> bool
{
    return m_developer;
}

void Imgui_window::set_developer()
{
    m_developer = true;
}

} // namespace erhe::imgui
