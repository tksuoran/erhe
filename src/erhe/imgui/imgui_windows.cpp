#include "erhe/imgui/imgui_windows.hpp"

#include "erhe/toolkit/window.hpp"
#include "erhe/commands/commands.hpp"
#include "erhe/configuration/configuration.hpp"
#include "erhe/imgui/imgui_log.hpp"
#include "erhe/imgui/imgui_renderer.hpp"
#include "erhe/imgui/imgui_viewport.hpp"
#include "erhe/imgui/imgui_window.hpp"
#include "erhe/imgui/scoped_imgui_context.hpp"
#include "erhe/imgui/window_imgui_viewport.hpp"
#include "erhe/rendergraph/rendergraph.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::imgui
{

Imgui_windows::Imgui_windows(
    Imgui_renderer&                 imgui_renderer,
    erhe::toolkit::Context_window*  context_window,
    erhe::rendergraph::Rendergraph& rendergraph
)
    : m_rendergraph{rendergraph}
{
    if (context_window != nullptr) {
        m_window_imgui_viewport = std::make_shared<erhe::imgui::Window_imgui_viewport>(
            imgui_renderer,
            *context_window,
            rendergraph,
            *this,
            "window_imgui_viewport"
        );
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

[[nodiscard]] auto Imgui_windows::get_window_viewport() -> std::shared_ptr<Window_imgui_viewport>
{
    return m_window_imgui_viewport;
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

void Imgui_windows::register_imgui_viewport(
    Imgui_viewport* viewport
)
{
    ERHE_VERIFY(!m_iterating);
    const std::lock_guard<std::recursive_mutex> lock{m_mutex};
#if !defined(NDEBUG)
    const auto i = std::find_if(
        m_imgui_viewports.begin(),
        m_imgui_viewports.end(),
        [viewport](Imgui_viewport* entry) {
            return entry == viewport;
        }
    );
    if (i != m_imgui_viewports.end()) {
        log_imgui->error("Imgui_viewport '{}' is already registered to Imgui_windows", viewport->get_name());
        return;
    }
#endif

    m_imgui_viewports.push_back(viewport);

}

void Imgui_windows::unregister_imgui_viewport(
    Imgui_viewport* viewport
)
{
    ERHE_VERIFY(!m_iterating);

    const std::lock_guard<std::recursive_mutex> lock{m_mutex};
    const auto i = std::find_if(
        m_imgui_viewports.begin(),
        m_imgui_viewports.end(),
        [viewport](Imgui_viewport* entry) {
            return entry == viewport;
        }
    );
    if (i == m_imgui_viewports.end()) {
        log_imgui->error("Imgui_windows::unregister_imgui_viewport(): viewport '{}' is not registered", viewport->get_name());
        return;
    }
    m_imgui_viewports.erase(i);
}

void Imgui_windows::make_current(const Imgui_viewport* imgui_viewport)
{
    m_current_viewport = imgui_viewport;
    if (imgui_viewport != nullptr) {
        ImGui::SetCurrentContext(imgui_viewport->imgui_context());
    } else {
        ImGui::SetCurrentContext(nullptr);
    }
}

void Imgui_windows::register_imgui_window(Imgui_window* window)
{
    bool show_window{false};
    const char* ini_label = window->ini_label();
    if (ini_label != nullptr) {
        auto ini = erhe::configuration::get_ini("erhe.ini", "windows");
        ini->get(ini_label, show_window);
    }
    register_imgui_window(window, show_window);
}

void Imgui_windows::register_imgui_window(
    Imgui_window* window,
    const bool    visible
)
{
    ERHE_VERIFY(!m_iterating);
    const std::lock_guard<std::recursive_mutex> lock{m_mutex};

    if (!visible) {
        window->hide();
    }

#ifndef NDEBUG
    const auto i = std::find(m_imgui_windows.begin(), m_imgui_windows.end(), window);
    if (i != m_imgui_windows.end()) {
        log_windows->error("Window {} already registered as ImGui Window", window->title());
    } else
#endif
    {
        m_imgui_windows.emplace_back(window);
        std::sort(
            m_imgui_windows.begin(),
            m_imgui_windows.end(),
            [](const Imgui_window* lhs, const Imgui_window* rhs)
            {
                return lhs->title() < rhs->title();
            }
        );
    }

    window->set_viewport(m_window_imgui_viewport.get());
}

void Imgui_windows::imgui_windows()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(!m_iterating);

    //Scoped_imgui_context scoped_context{m_imgui_context};
    m_iterating = true;
    for (const auto& viewport : m_imgui_viewports) {
        Scoped_imgui_context imgui_context{*viewport};

        if (viewport->begin_imgui_frame()) {
            std::size_t i = 0;

            bool window_wants_keyboard{false};
            bool window_wants_mouse   {false};

            for (auto& imgui_window : m_imgui_windows) {
                if (imgui_window->get_viewport() != viewport) {
                    continue;
                }
                bool hidden = true;
                bool toolbar_hovered = false;
                if (imgui_window->is_visible()) {
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

            viewport->update_input_request(window_wants_keyboard, window_wants_mouse);
            viewport->end_imgui_frame();
        }
    }
    m_iterating = false;
    flush_queue();
}

void Imgui_windows::window_menu(Imgui_viewport* imgui_viewport)
{
    ERHE_VERIFY(m_current_viewport != nullptr);
    bool was_iterating = m_iterating;
    m_iterating = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});

    if (ImGui::BeginMenu("Window")) {
        for (const auto& window : m_imgui_windows) {
            if (!window->show_in_menu()) {
                continue;
            }
            bool enabled = window->is_visible();
            if (ImGui::MenuItem(window->title().data(), "", &enabled)) {
                if (enabled) {
                    window->show();
                    window->set_viewport(imgui_viewport);
                } else {
                    window->hide();
                }
            }
        }

        ImGui::Separator();

        imgui_viewport->builtin_imgui_window_menu();

        ImGui::Separator();
        if (ImGui::MenuItem("Close All")) {
            for (const auto& window : m_imgui_windows) {
                window->hide();
            }
        }
        if (ImGui::MenuItem("Open All")) {
            for (const auto& window : m_imgui_windows) {
                window->show();
            }
        }
        ImGui::EndMenu();
    }

    ImGui::PopStyleVar();
    m_iterating = was_iterating;
}

auto Imgui_windows::get_windows() -> std::vector<Imgui_window*>&
{
    ERHE_VERIFY(!m_iterating);
    return m_imgui_windows;
}

[[nodiscard]] auto Imgui_windows::want_capture_keyboard() const -> bool
{
    return m_window_imgui_viewport
        ? m_window_imgui_viewport->want_capture_keyboard()
        : false;
}

[[nodiscard]] auto Imgui_windows::want_capture_mouse() const -> bool
{
    return m_window_imgui_viewport
        ? m_window_imgui_viewport->want_capture_mouse()
        : false;
}

auto Imgui_windows::on_focus(const int focused) -> bool
{
    if (!m_window_imgui_viewport) {
        return false;
    }

    m_window_imgui_viewport->on_focus(focused);

    return false; // does not consume
}

auto Imgui_windows::on_cursor_enter(const int entered) -> bool
{
    if (!m_window_imgui_viewport) {
        return false;
    }

    m_window_imgui_viewport->on_cursor_enter(entered);
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

auto Imgui_windows::on_mouse_move(
    const float x,
    const float y
) -> bool
{
    if (!m_window_imgui_viewport) {
        return false;
    }

    // for (auto& imgui_window : m_imgui_windows)
    // {
    //     if (imgui_window->is_hovered())
    //     {
    //     }
    // }

    m_window_imgui_viewport->on_mouse_move(x, y);

    // If ImGui wants to capture mouse, the mouse event is not passed
    // to lower priority Window_event_handlers (Commands).
    return want_capture_mouse();
}

auto Imgui_windows::on_mouse_button(
    const uint32_t button,
    const bool     pressed
) -> bool
{
    for (const auto& viewport : m_imgui_viewports) {
        viewport->on_mouse_button(button, pressed);
    }

    // If ImGui wants to capture mouse, the mouse event is not passed
    // to lower priority Window_event_handlers (Commands).
    return want_capture_mouse();
}

auto Imgui_windows::on_mouse_wheel(
    const float x,
    const float y
) -> bool
{
    for (const auto& viewport : m_imgui_viewports) {
        viewport->on_mouse_wheel(x, y);
    }

    // If ImGui wants to capture mouse, the mouse event is not passed
    // to lower priority Window_event_handlers (Commands).
    return want_capture_mouse();
}

auto Imgui_windows::on_key(
    const signed int keycode,
    const uint32_t   modifier_mask,
    const bool       pressed
) -> bool
{
    for (const auto& viewport : m_imgui_viewports) {
        viewport->on_key(keycode, modifier_mask, pressed);
    }

    return want_capture_keyboard();
}

auto Imgui_windows::on_char(
    const unsigned int codepoint
) -> bool
{
    for (const auto& viewport : m_imgui_viewports) {
        viewport->on_char(codepoint);
    }

    return want_capture_keyboard();
}

}  // namespace erhe::imgui
