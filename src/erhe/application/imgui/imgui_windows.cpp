#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/imgui/imgui_viewport.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/application/imgui/scoped_imgui_context.hpp"
#include "erhe/application/imgui/window_imgui_viewport.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/window.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::application
{

Imgui_windows* g_imgui_windows{nullptr};

Imgui_windows::Imgui_windows()
    : erhe::components::Component{c_type_name}
{
}

Imgui_windows::~Imgui_windows() noexcept
{
    ERHE_VERIFY(g_imgui_windows == nullptr);
}

void Imgui_windows::deinitialize_component()
{
    ERHE_VERIFY(g_imgui_windows == this);
    m_imgui_viewports.clear();
    m_imgui_windows.clear();
    m_current_viewport = nullptr;
    m_window_imgui_viewport.reset();
    g_imgui_windows = nullptr;
}

void Imgui_windows::declare_required_components()
{
    require <Configuration >();
    require <Imgui_renderer>();
    optional<Rendergraph   >();
}

void Imgui_windows::initialize_component()
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(g_imgui_windows == nullptr);
    ERHE_VERIFY(g_configuration != nullptr); // assert it has been initialized

    if (g_configuration->imgui.window_viewport) {
        m_window_imgui_viewport = std::make_shared<Window_imgui_viewport>(
            "window_imgui_viewport"
        );

        register_imgui_viewport(m_window_imgui_viewport);
    }

    g_imgui_windows = this;
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
    const std::shared_ptr<Imgui_viewport>& viewport
)
{
    ERHE_VERIFY(!m_iterating);
    const std::lock_guard<std::recursive_mutex> lock{m_mutex};
    m_imgui_viewports.emplace_back(viewport);

    if (g_rendergraph != nullptr) {
        g_rendergraph->register_node(viewport);
    }
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

void Imgui_windows::register_imgui_window(Imgui_window* window, const char* ini_entry)
{
    bool show_window{false};
    if (ini_entry != nullptr) {
        auto ini = get_ini("erhe.ini", "windows");
        ini->get(ini_entry, show_window);
    }
    register_imgui_window(window, show_window);
}

void Imgui_windows::register_imgui_window(Imgui_window* window, const bool visible)
{
    ERHE_VERIFY(!m_iterating);
    const std::lock_guard<std::recursive_mutex> lock{m_mutex};

    if (!visible) {
        window->hide();
    }

    window->set_viewport(m_window_imgui_viewport.get());

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
}

void Imgui_windows::imgui_windows()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(!m_iterating);

    //Scoped_imgui_context scoped_context{m_imgui_context};
    m_iterating = true;
    for (const auto& viewport : m_imgui_viewports) {
        Scoped_imgui_context imgui_context{*viewport.get()};

        if (viewport->begin_imgui_frame()) {
            std::size_t i = 0;

            bool window_wants_keyboard{false};
            bool window_wants_mouse   {false};

            for (auto& imgui_window : m_imgui_windows) {
                if (imgui_window->get_viewport() != viewport.get()) {
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

void Imgui_windows::on_focus(int focused)
{
    if (!m_window_imgui_viewport) {
        return;
    }

    m_window_imgui_viewport->on_focus(focused);
}

void Imgui_windows::on_cursor_enter(int entered)
{
    if (!m_window_imgui_viewport) {
        return;
    }

    m_window_imgui_viewport->on_cursor_enter(entered);
}

void Imgui_windows::on_mouse_move(
    const float x,
    const float y
)
{
    if (!m_window_imgui_viewport) {
        return;
    }

    // for (auto& imgui_window : m_imgui_windows)
    // {
    //     if (imgui_window->is_hovered())
    //     {
    //     }
    // }

    m_window_imgui_viewport->on_mouse_move(x, y);
}

void Imgui_windows::on_mouse_button(
    const uint32_t button,
    const bool     pressed
)
{
    for (const auto& viewport : m_imgui_viewports) {
        viewport->on_mouse_button(button, pressed);
    }
}

void Imgui_windows::on_mouse_wheel(
    const float x,
    const float y
)
{
    for (const auto& viewport : m_imgui_viewports) {
        viewport->on_mouse_wheel(x, y);
    }
}

void Imgui_windows::on_key(
    const signed int keycode,
    const uint32_t   modifier_mask,
    const bool       pressed
)
{
    for (const auto& viewport : m_imgui_viewports) {
        viewport->on_key(keycode, modifier_mask, pressed);
    }
}

void Imgui_windows::on_char(
    const unsigned int codepoint
)
{
    for (const auto& viewport : m_imgui_viewports) {
        viewport->on_char(codepoint);
    }
}

}  // namespace editor
