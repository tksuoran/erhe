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

namespace erhe::application
{

Imgui_windows::Imgui_windows()
    : erhe::components::Component{c_type_name}
{
}

Imgui_windows::~Imgui_windows() noexcept
{
}

void Imgui_windows::declare_required_components()
{
    require<Configuration>();
    m_imgui_renderer = require<Imgui_renderer>();
    m_render_graph   = require<Rendergraph>();
}

void Imgui_windows::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const auto& configuration = get<Configuration>();

    if (configuration->imgui.window_viewport)
    {
        m_window_imgui_viewport = std::make_shared<Window_imgui_viewport>(
            "window_imgui_viewport",
            *m_components
        );

        register_imgui_viewport(m_window_imgui_viewport);
    }
}

void Imgui_windows::post_initialize()
{
    m_commands = get<Commands>();

    if (m_window_imgui_viewport)
    {
        m_window_imgui_viewport->post_initialize(*m_components);
    }
}

[[nodiscard]] auto Imgui_windows::get_mutex() -> std::mutex&
{
    return m_mutex;
}

[[nodiscard]] auto Imgui_windows::get_window_viewport() -> std::shared_ptr<Window_imgui_viewport>
{
    return m_window_imgui_viewport;
}

[[nodiscard]] auto Imgui_windows::get_imgui_builtin_windows() -> Imgui_builtin_windows&
{
    return m_imgui_builtin_windows;
}

void Imgui_windows::register_imgui_viewport(
    const std::shared_ptr<Imgui_viewport>& viewport
)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_imgui_viewports.emplace_back(viewport);

    m_render_graph->register_node(viewport);
}

void Imgui_windows::make_current(const Imgui_viewport* imgui_viewport)
{
    m_current_viewport = imgui_viewport;
    if (imgui_viewport != nullptr)
    {
        ImGui::SetCurrentContext(imgui_viewport->imgui_context());
    }
    else
    {
        ImGui::SetCurrentContext(nullptr);
    }
}

void Imgui_windows::register_imgui_window(Imgui_window* window)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    window->initialize(*m_components);
    window->set_viewport(m_window_imgui_viewport.get());

#ifndef NDEBUG
    const auto i = std::find(m_imgui_windows.begin(), m_imgui_windows.end(), window);
    if (i != m_imgui_windows.end())
    {
        log_windows->error("Window {} already registered as ImGui Window", window->title());
    }
    else
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
    ERHE_PROFILE_FUNCTION

    //Scoped_imgui_context scoped_context{m_imgui_context};

    for (const auto& viewport : m_imgui_viewports)
    {
        Scoped_imgui_context imgui_context{*this, *viewport.get()};

        if (viewport->begin_imgui_frame())
        {
            std::size_t i = 0;

            bool window_wants_keyboard{false};
            bool window_wants_mouse   {false};

            for (auto& imgui_window : m_imgui_windows)
            {
                if (imgui_window->get_viewport() != viewport.get())
                {
                    continue;
                }
                if (imgui_window->is_visible())
                {
                    auto window_id = fmt::format("##window-{}", ++i);
                    ImGui::PushID(window_id.c_str());
                    const auto is_window_visible = imgui_window->begin();
                    if (is_window_visible)
                    {
                        imgui_window->imgui();
                    }
                    const auto window_position    = ImGui::GetWindowPos();
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

                    window_wants_keyboard = window_wants_keyboard || imgui_window->want_keyboard_events();
                    window_wants_mouse    = window_wants_mouse    || imgui_window->want_mouse_events();
                    imgui_window->end();
                    ImGui::PopID();

                    if (is_window_visible && imgui_window->has_toolbar())
                    {
                        auto toolbar_id = fmt::format("##window-{}-toolbar", ++i);
                        ImGui::SetNextWindowPos(
                            ImVec2{
                                window_position.x + content_region_min.x,
                                window_position.y + content_region_min.y
                            },
                            ImGuiCond_Always
                        );
                        ImGui::SetNextWindowSize(content_region_size, ImGuiCond_Always);
                        ImGui::SetNextWindowBgAlpha(0.0f); // Transparent background
                        constexpr ImGuiWindowFlags toolbar_window_flags =
                            ImGuiWindowFlags_NoDecoration       |
                            ImGuiWindowFlags_NoDocking          |
                            ImGuiWindowFlags_AlwaysAutoResize   |
                            ImGuiWindowFlags_NoSavedSettings    |
                            ImGuiWindowFlags_NoFocusOnAppearing |
                            ImGuiWindowFlags_NoNav;

                        if (ImGui::Begin(toolbar_id.c_str(), nullptr, toolbar_window_flags))
                        {
                            imgui_window->toolbar();
                        }
                        ImGui::End();
                    }
                }
            }

            viewport->update_input_request(window_wants_keyboard, window_wants_mouse);
            viewport->end_imgui_frame();
        }
    }
}

void Imgui_windows::window_menu()
{
    ERHE_VERIFY(m_current_viewport != nullptr);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});

    if (ImGui::BeginMenu("Window"))
    {
        for (const auto& window : m_imgui_windows)
        {
            bool enabled = window->is_visible();
            if (ImGui::MenuItem(window->title().data(), "", &enabled))
            {
                if (enabled)
                {
                    window->show();
                }
                else
                {
                    window->hide();
                }
            }
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("ImGui"))
        {
            ImGui::MenuItem("Demo",             "", &m_imgui_builtin_windows.demo);
            ImGui::MenuItem("Style Editor",     "", &m_imgui_builtin_windows.style_editor);
            ImGui::MenuItem("Metrics/Debugger", "", &m_imgui_builtin_windows.metrics);
            ImGui::MenuItem("Stack Tool",       "", &m_imgui_builtin_windows.stack_tool);
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Close All"))
        {
            for (const auto& window : m_imgui_windows)
            {
                window->hide();
            }
        }
        if (ImGui::MenuItem("Open All"))
        {
            for (const auto& window : m_imgui_windows)
            {
                window->show();
            }
        }
        ImGui::EndMenu();
    }

    ImGui::PopStyleVar();
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
    if (!m_window_imgui_viewport)
    {
        return;
    }

    Scoped_imgui_context scoped_imgui_context{*this, *m_window_imgui_viewport.get()};

    m_window_imgui_viewport->on_focus(focused);
}

void Imgui_windows::on_cursor_enter(int entered)
{
    if (!m_window_imgui_viewport)
    {
        return;
    }

    Scoped_imgui_context scoped_imgui_context{*this, *m_window_imgui_viewport.get()};

    m_window_imgui_viewport->on_cursor_enter(entered);
}

void Imgui_windows::on_mouse_move(
    const double x,
    const double y
)
{
    if (!m_window_imgui_viewport)
    {
        return;
    }

    Scoped_imgui_context scoped_imgui_context{*this, *m_window_imgui_viewport.get()};

    m_window_imgui_viewport->on_mouse_move(x, y);
}

void Imgui_windows::on_mouse_click(
    const uint32_t button,
    const int      count
)
{
    for (const auto& viewport : m_imgui_viewports)
    {
        Scoped_imgui_context scoped_imgui_context{*this, *viewport.get()};
        viewport->on_mouse_click(button, count);
    }
}

void Imgui_windows::on_mouse_wheel(
    const double x,
    const double y
)
{
    for (const auto& viewport : m_imgui_viewports)
    {
        Scoped_imgui_context scoped_imgui_context{*this, *viewport.get()};
        viewport->on_mouse_wheel(x, y);
    }
}

void Imgui_windows::on_key(
    const signed int keycode,
    const uint32_t   modifier_mask,
    const bool       pressed
)
{
    for (const auto& viewport : m_imgui_viewports)
    {
        Scoped_imgui_context scoped_imgui_context{*this, *viewport.get()};
        viewport->on_key(keycode, modifier_mask, pressed);
    }
}

void Imgui_windows::on_char(
    const unsigned int codepoint
)
{
    for (const auto& viewport : m_imgui_viewports)
    {
        Scoped_imgui_context scoped_imgui_context{*this, *viewport.get()};
        viewport->on_char(codepoint);
    }
}

}  // namespace editor
