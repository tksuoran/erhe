// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

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
#include "erhe_window/window.hpp"

namespace erhe::imgui {

Imgui_windows::Imgui_windows(
    Imgui_renderer&                 imgui_renderer,
    erhe::graphics::Device&         graphics_device,
    erhe::rendergraph::Rendergraph& rendergraph,
    erhe::window::Context_window*   context_window,
    std::string_view                windows_ini_path
)
    : m_imgui_renderer  {imgui_renderer}
    , m_context_window  {context_window}
    , m_rendergraph     {rendergraph}
    , m_windows_ini_path{windows_ini_path}
{
    ERHE_PROFILE_FUNCTION();

    if (context_window != nullptr) {
        m_window_imgui_host = std::make_shared<erhe::imgui::Window_imgui_host>(
            imgui_renderer,
            graphics_device,
            rendergraph,
            *context_window,
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
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_queued_operations_mutex};
    m_queued_operations.push_back(std::move(operation));
}

void Imgui_windows::flush_queue()
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_queued_operations_mutex}; // TODO Can this be avoided?
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

auto Imgui_windows::get_persistent_window_open(std::string_view ini_label) const -> bool
{
    bool result = true;
    if (!m_windows_ini_path.empty()) {
        const auto& ini = erhe::configuration::get_ini_file_section(m_windows_ini_path.c_str(), "windows");
        ini.get(ini_label.data(), result);
    }
    return result;
}

void Imgui_windows::save_window_state()
{
    if (m_windows_ini_path.empty()) {
        return;
    }
    toml::table table;
    for (auto& imgui_window : m_imgui_windows) {
        const auto label = imgui_window->get_ini_label();
        if (label.empty()) {
            continue;
        }
        table.insert(label.c_str(), imgui_window->is_window_visible());
    }
    toml::table root_table;
    root_table.insert("windows", table);
    const bool save_ok = erhe::configuration::write_toml(root_table, m_windows_ini_path);
    if (!save_ok) {
        log_imgui->warn("Imgui_windows::save_window_state() failed to write {}", m_windows_ini_path.c_str());
    }
}

void Imgui_windows::process_events(const float dt_s, const int64_t time_ns)
{
    ERHE_VERIFY(!m_iterating);
    m_iterating = true;
    const auto& imgui_hosts = m_imgui_renderer.get_imgui_hosts();
    for (const auto& imgui_host : imgui_hosts) {
        Scoped_imgui_context imgui_context{*imgui_host};
        imgui_host->process_events(dt_s, time_ns);
    }
    m_iterating = false;
}

void Imgui_windows::begin_frame()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(!m_iterating);
    m_imgui_renderer.begin_frame();

    m_iterating = true;
    const auto& imgui_hosts = m_imgui_renderer.get_imgui_hosts();
    for (const auto& imgui_host : imgui_hosts) {
        Scoped_imgui_context imgui_context{*imgui_host};
        imgui_host->begin_imgui_frame();
    }
    m_iterating = false;
}

void Imgui_windows::end_frame()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(!m_iterating);

    m_iterating = true;
    const auto& imgui_hosts = m_imgui_renderer.get_imgui_hosts();
    for (const auto& imgui_host : imgui_hosts) {
        Scoped_imgui_context imgui_context{*imgui_host};

        if (imgui_host->is_visible()) {
            imgui_host->end_imgui_frame();
        }
    }
    m_iterating = false;
}

void Imgui_windows::draw_imgui_windows()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(!m_iterating);

    m_iterating = true;
    int window_id = 0;
    const auto& imgui_hosts = m_imgui_renderer.get_imgui_hosts();
    for (const auto& imgui_host : imgui_hosts) {
        Scoped_imgui_context imgui_context{*imgui_host};

        if (imgui_host->is_visible()) {
            bool window_wants_keyboard{false};
            bool window_wants_mouse   {false};

            for (auto& imgui_window : m_imgui_windows) {
                if (imgui_window->get_imgui_host() != imgui_host) {
                    continue;
                }
                bool hidden = true;
                if (imgui_window->is_window_visible()) {
                    bool toolbar_hovered = false;
                    ImGui::PushID(++window_id);
                    const bool is_window_visible = imgui_window->begin();
                    if (is_window_visible) {
                        const ImVec2 before_cursor_pos = ImGui::GetCursorPos();
                        imgui_window->imgui();
                        hidden = false;
                        if (imgui_window->has_toolbar()) {
                            ImGui::SetCursorPos(before_cursor_pos);
                            imgui_window->toolbar(toolbar_hovered);
                        }
                    }
                    const bool window_hovered = ImGui::IsWindowHovered();
                    if (!toolbar_hovered && window_hovered) {
                        window_wants_keyboard = window_wants_keyboard || imgui_window->want_keyboard_events();
                        window_wants_mouse    = window_wants_mouse    || imgui_window->want_mouse_events();
                    }
                    if (imgui_window->want_cursor_relative_hold()) {
                        imgui_host->request_cursor_relative_hold();
                    }

                    imgui_window->end();

                    ImGui::PopID();
                }
                if (hidden) {
                    imgui_window->hidden();
                }
            }

            imgui_host->update_input_request(window_wants_keyboard, window_wants_mouse);
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
            ImGui::Text("Want capture mouse: %s",      imgui_host->want_capture_mouse()    ? "Yes" : "No");
            ImGui::Text("Want capture keyboard: %s",   imgui_host->want_capture_keyboard() ? "Yes" : "No");
            ImGui::Text("Window request mouse: %s",    imgui_host->get_window_request_mouse()    ? "Yes" : "No");
            ImGui::Text("Window request keyboard: %s", imgui_host->get_window_request_keyboard() ? "Yes" : "No");
            for (auto& imgui_window : m_imgui_windows) {
                if (imgui_window->get_imgui_host() != imgui_host) {
                    continue;
                }
                if (ImGui::TreeNodeEx(imgui_window->get_title().c_str())) {
                    ImGui::Text("Visible: %s",       imgui_window->is_window_visible()    ? "Yes" : "No");
                    ImGui::Text("Hovered: %s",       imgui_window->is_window_hovered()    ? "Yes" : "No");
                    ImGui::Text("Show in menu: %s",  imgui_window->show_in_menu()         ? "Yes" : "No");
                    ImGui::Text("Has toolbar: %s",   imgui_window->has_toolbar()          ? "Yes" : "No");
                    ImGui::Text("Want mouse: %s",    imgui_window->want_mouse_events()    ? "Yes" : "No");
                    ImGui::Text("Want keyboard: %s", imgui_window->want_keyboard_events() ? "Yes" : "No");
                    ImGui::Text("Scale: %f",         imgui_window->get_scale_value());
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }
}

void Imgui_windows::window_menu_entries(Imgui_host& imgui_host, bool developer)
{
    bool was_iterating = m_iterating;
    m_iterating = true;

    for (const auto& window : m_imgui_windows) {
        if (!window->show_in_menu()) {
            continue;
        }
        if (window->show_in_developer_menu() != developer) {
            continue;
        }
        bool enabled = window->is_window_visible();
        if (ImGui::MenuItem(window->get_title().data(), "", &enabled)) {
            if (enabled) {
                window->show_window();
                window->set_imgui_host(&imgui_host);
            } else {
                window->hide_window();
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
    return m_window_imgui_host ? m_window_imgui_host->want_capture_keyboard() : false;
}

auto Imgui_windows::want_capture_mouse() const -> bool
{
    return m_window_imgui_host ? m_window_imgui_host->want_capture_mouse() : false;
}

auto Imgui_windows::on_window_focus_event(const erhe::window::Input_event& input_event) -> bool
{
    SPDLOG_LOGGER_TRACE(log_input_events, "Imgui_host::on_window_focus_event({})", input_event.u.window_focus_event.focused);
    if (!m_window_imgui_host) {
        SPDLOG_LOGGER_TRACE(log_input_events, "  m_window_imgui_host");
        return false;
    }

    m_window_imgui_host->on_window_focus_event(input_event);

    return false; // does not consume
}

auto Imgui_windows::on_cursor_enter_event(const erhe::window::Input_event& input_event) -> bool
{
    if (!m_window_imgui_host) {
        return false;
    }

    m_window_imgui_host->on_cursor_enter_event(input_event);
    return false; // does not consume
}

auto Imgui_windows::on_mouse_move_event(const erhe::window::Input_event& input_event) -> bool
{
    if (!m_window_imgui_host) {
        return false;
    }

    m_window_imgui_host->on_mouse_move_event(input_event);

    // If ImGui wants to capture mouse, the mouse event is marked as handled
    return want_capture_mouse();
}

auto Imgui_windows::on_mouse_button_event(const erhe::window::Input_event& input_event) -> bool
{
    const auto& imgui_hosts = m_imgui_renderer.get_imgui_hosts();
    for (const auto& host : imgui_hosts) {
        host->on_mouse_button_event(input_event);
    }

    // If ImGui wants to capture mouse, the mouse event is marked as handled
    return want_capture_mouse();
}

auto Imgui_windows::on_mouse_wheel_event(const erhe::window::Input_event& input_event) -> bool
{
    const auto& imgui_hosts = m_imgui_renderer.get_imgui_hosts();
    for (const auto& host : imgui_hosts) {
        host->on_mouse_wheel_event(input_event);
    }

    // If ImGui wants to capture mouse, the mouse event is marked as handled
    return want_capture_mouse();
}

auto Imgui_windows::on_key_event(const erhe::window::Input_event& input_event) -> bool
{
    const auto& imgui_hosts = m_imgui_renderer.get_imgui_hosts();
    for (const auto& host : imgui_hosts) {
        host->on_key_event(input_event);
    }

    return want_capture_keyboard();
}

auto Imgui_windows::on_char_event(const erhe::window::Input_event& input_event) -> bool
{
    const auto& imgui_hosts = m_imgui_renderer.get_imgui_hosts();
    for (const auto& host : imgui_hosts) {
        host->on_char_event(input_event);
    }

    return want_capture_keyboard();
}

}  // namespace erhe::imgui
