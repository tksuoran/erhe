#include "erhe_imgui/imgui_windows.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_imgui/imgui_log.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/scoped_imgui_context.hpp"
#include "erhe_imgui/window_imgui_host.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::imgui {

Imgui_windows::Imgui_windows(
    Imgui_renderer&                 imgui_renderer,
    erhe::window::Context_window*   context_window,
    erhe::rendergraph::Rendergraph& rendergraph
)
    : m_imgui_renderer{imgui_renderer}
    , m_rendergraph   {rendergraph}
{
    if (context_window != nullptr) {
        m_window_imgui_host = std::make_shared<erhe::imgui::Window_imgui_host>(
            imgui_renderer,
            *context_window,
            rendergraph,
            "window_imgui_host"
        );
    }
}

auto Imgui_windows::get_window_imgui_host() -> std::shared_ptr<Window_imgui_host>
{
    return m_window_imgui_host;
}

void Imgui_windows::queue(std::function<void()>&& operation)
{
    const std::lock_guard<std::mutex> lock{m_queued_operations_mutex};
    m_queued_operations.push_back(std::move(operation));
}

void Imgui_windows::flush_queue()
{
    const std::lock_guard<std::mutex> lock{m_queued_operations_mutex}; // TODO Can this be avoided?
    while (!m_queued_operations.empty()) {
        auto op = m_queued_operations.back();
        m_queued_operations.pop_back();
        op();
    }
}

void Imgui_windows::lock_mutex()
{
    m_mutex.lock();
}

void Imgui_windows::unlock_mutex()
{
    m_mutex.unlock();
}

void Imgui_windows::register_imgui_window(Imgui_window* window)
{
    ERHE_VERIFY(!m_iterating);
    const std::lock_guard<std::recursive_mutex> lock{m_mutex};

#ifndef NDEBUG
    const auto i = std::find(m_imgui_windows.begin(), m_imgui_windows.end(), window);
    if (i != m_imgui_windows.end()) {
        log_windows->error("Window {} already registered as ImGui Window", window->get_title());
    } else
#endif
    {
        m_imgui_windows.emplace_back(window);
        std::sort(
            m_imgui_windows.begin(),
            m_imgui_windows.end(),
            [](const Imgui_window* lhs, const Imgui_window* rhs) {
                return lhs->get_title() < rhs->get_title();
            }
        );
    }

    window->set_imgui_host(m_window_imgui_host.get());
}

void Imgui_windows::unregister_imgui_window(Imgui_window* window)
{
    ERHE_VERIFY(!m_iterating);
    const std::lock_guard<std::recursive_mutex> lock{m_mutex};

    const auto i = std::remove(m_imgui_windows.begin(), m_imgui_windows.end(), window);
    if (i == m_imgui_windows.end()) {
        log_windows->error("Imgui_window 'title = {}, ini_label = {}' is not registered in Imgui_windows", window->get_title(), window->get_ini_label());
    } else {
        m_imgui_windows.erase(i, m_imgui_windows.end());
    }
}

void Imgui_windows::save_window_state()
{
    mINI::INIFile file("windows.ini");
    mINI::INIStructure ini;
    for (auto& imgui_window : m_imgui_windows) {
        const auto label = imgui_window->get_ini_label();
        if (label.empty()) {
            continue;
        }
        ini["windows"][label.c_str()] = imgui_window->is_visible() ? "true" : "false";
    }
    file.generate(ini);
}

void Imgui_windows::imgui_windows()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(!m_iterating);

    //Scoped_imgui_context scoped_context{m_imgui_context};
    m_iterating = true;
    const auto& imgui_hosts = m_imgui_renderer.get_imgui_hosts();
    for (const auto& imgui_host : imgui_hosts) {
        Scoped_imgui_context imgui_context{*imgui_host};

        if (imgui_host->begin_imgui_frame()) {
            std::size_t i = 0;

            bool window_wants_keyboard{false};
            bool window_wants_mouse   {false};

            for (auto& imgui_window : m_imgui_windows) {
                if (imgui_window->get_imgui_host() != imgui_host) {
                    continue;
                }
                bool hidden = true;
                if (imgui_window->is_visible()) {
                    bool toolbar_hovered = false;
                    auto window_id = fmt::format("##window-{}", ++i);
                    ImGui::PushID(window_id.c_str());
                    const auto is_window_visible = imgui_window->begin();
                    if (is_window_visible) {
                        const auto before_cursor_pos = ImGui::GetCursorPos();
                        imgui_window->imgui();
                        hidden = false;
                        if (imgui_window->has_toolbar()) {
                            ImGui::SetCursorPos(before_cursor_pos);
                            imgui_window->toolbar(toolbar_hovered);
                        }
                    }
                    const auto window_position    = ImGui::GetWindowPos();
                    const auto window_size        = ImGui::GetWindowSize();
                    const auto content_region_min = ImGui::GetWindowContentRegionMin();
                    const auto content_region_max = ImGui::GetWindowContentRegionMin();
                    const ImVec2 content_region_size{
                        content_region_max.x - content_region_min.x,
                        content_region_max.y - content_region_min.y
                    };
                    const ImVec2 toolbar_window_position
                    {
                        window_position.x + content_region_min.x,
                        window_position.y + content_region_min.y
                    };
                    const bool window_hovered = ImGui::IsWindowHovered();
                    if (!toolbar_hovered && window_hovered) {
                        window_wants_keyboard = window_wants_keyboard || imgui_window->want_keyboard_events();
                        window_wants_mouse    = window_wants_mouse    || imgui_window->want_mouse_events();
                    }

                    imgui_window->end();

                    ImGui::PopID();
                }
                if (hidden) {
                    imgui_window->hidden();
                }
            }

            imgui_host->update_input_request(window_wants_keyboard, window_wants_mouse);
            imgui_host->end_imgui_frame();
        }
    }
    m_iterating = false;
    flush_queue();
}

void Imgui_windows::debug_imgui()
{
    const auto& imgui_hosts = m_imgui_renderer.get_imgui_hosts();
    for (const auto& imgui_host : imgui_hosts) {
        if (ImGui::TreeNodeEx(imgui_host->name().c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (auto& imgui_window : m_imgui_windows) {
                if (imgui_window->get_imgui_host() != imgui_host) {
                    continue;
                }
                if (ImGui::TreeNodeEx(imgui_window->get_title().c_str())) {
                    ImGui::Text("Visible: %s",       imgui_window->is_visible()   ? "Yes" : "No");
                    ImGui::Text("Hovered: %s",       imgui_window->is_hovered()   ? "Yes" : "No");
                    ImGui::Text("Show in menu: %s",  imgui_window->show_in_menu() ? "Yes" : "No");
                    ImGui::Text("Has toolbar: %s",   imgui_window->has_toolbar()  ? "Yes" : "No");
                    ImGui::Text("Want mouse: %s",    imgui_window->want_mouse_events() ? "Yes" : "No");
                    ImGui::Text("Want keyboard: %s", imgui_window->want_keyboard_events() ? "Yes" : "No");
                    ImGui::Text("Scale: %f",         imgui_window->get_scale_value());
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }
}

void Imgui_windows::window_menu_entries(Imgui_host& imgui_host)
{
    bool was_iterating = m_iterating;
    m_iterating = true;

    for (const auto& window : m_imgui_windows) {
        if (!window->show_in_menu()) {
            continue;
        }
        bool enabled = window->is_visible();
        if (ImGui::MenuItem(window->get_title().data(), "", &enabled)) {
            if (enabled) {
                window->show();
                window->set_imgui_host(&imgui_host);
            } else {
                window->hide();
            }
        }
    }

    m_iterating = was_iterating;
}

auto Imgui_windows::get_windows() -> std::vector<Imgui_window*>&
{
    //ERHE_VERIFY(!m_iterating);
    return m_imgui_windows;
}

auto Imgui_windows::want_capture_keyboard() const -> bool
{
    return m_window_imgui_host
        ? m_window_imgui_host->want_capture_keyboard()
        : false;
}

auto Imgui_windows::want_capture_mouse() const -> bool
{
    return m_window_imgui_host
        ? m_window_imgui_host->want_capture_mouse()
        : false;
}

auto Imgui_windows::on_focus(const int focused) -> bool
{
    if (!m_window_imgui_host) {
        return false;
    }

    m_window_imgui_host->on_focus(focused);

    return false; // does not consume
}

auto Imgui_windows::on_cursor_enter(const int entered) -> bool
{
    if (!m_window_imgui_host) {
        return false;
    }

    m_window_imgui_host->on_cursor_enter(entered);
    return false; // does not consume
}

//auto Imgui_windows::get_imgui_capture_mouse() const -> bool
//{
//    //const bool viewports_hosted_in_imgui =
//    //    g_window->config.show &&
//    //    g_window->config.window_viewport;
//
//    if (!viewports_hosted_in_imgui) {
//        return false;
//    }
//
//    return want_capture_mouse();
//}

auto Imgui_windows::on_mouse_move(float absolute_x, float absolute_y, float relative_x, float relative_y, uint32_t modifier_mask) -> bool
{
    static_cast<float>(relative_x);
    static_cast<float>(relative_y);
    static_cast<float>(modifier_mask);
    if (!m_window_imgui_host) {
        return false;
    }

    // for (auto& imgui_window : m_imgui_windows)
    // {
    //     if (imgui_window->is_hovered())
    //     {
    //     }
    // }

    m_window_imgui_host->on_mouse_move(absolute_x, absolute_y);

    // If ImGui wants to capture mouse, the mouse event is not passed
    // to lower priority Window_event_handlers (Commands).
    return want_capture_mouse();
}

auto Imgui_windows::on_mouse_button(const uint32_t button, const bool pressed, const uint32_t modifier_mask) -> bool
{
    static_cast<void>(modifier_mask);
    const auto& imgui_viewports = m_imgui_renderer.get_imgui_hosts();
    for (const auto& viewport : imgui_viewports) {
        viewport->on_mouse_button(button, pressed);
    }

    // If ImGui wants to capture mouse, the mouse event is not passed
    // to lower priority Window_event_handlers (Commands).
    return want_capture_mouse();
}

auto Imgui_windows::on_mouse_wheel(const float x, const float y, uint32_t) -> bool
{
    const auto& imgui_viewports = m_imgui_renderer.get_imgui_hosts();
    for (const auto& viewport : imgui_viewports) {
        viewport->on_mouse_wheel(x, y);
    }

    // If ImGui wants to capture mouse, the mouse event is not passed
    // to lower priority Window_event_handlers (Commands).
    return want_capture_mouse();
}

auto Imgui_windows::on_key(const signed int keycode, const uint32_t modifier_mask, const bool pressed) -> bool
{
    const auto& imgui_viewports = m_imgui_renderer.get_imgui_hosts();
    for (const auto& viewport : imgui_viewports) {
        viewport->on_key(keycode, modifier_mask, pressed);
    }

    return want_capture_keyboard();
}

auto Imgui_windows::on_char(const unsigned int codepoint) -> bool
{
    const auto& imgui_viewports = m_imgui_renderer.get_imgui_hosts();
    for (const auto& viewport : imgui_viewports) {
        viewport->on_char(codepoint);
    }

    return want_capture_keyboard();
}

}  // namespace erhe::imgui
