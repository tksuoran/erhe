#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/application/imgui/imgui_viewport.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/application/imgui/scoped_imgui_context.hpp"
#include "erhe/application/imgui/window_imgui_viewport.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/view.hpp"
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
    m_imgui_renderer = require<Imgui_renderer>();
    m_render_graph   = require<Rendergraph>();
}

void Imgui_windows::initialize_component()
{
    m_window_imgui_viewport = std::make_shared<Window_imgui_viewport>(
        "window_imgui_viewport",
        *m_components
    );

    register_imgui_viewport(m_window_imgui_viewport);
}

void Imgui_windows::post_initialize()
{
    m_view = get<View>();

    m_window_imgui_viewport->post_initialize(*m_components);
}

[[nodiscard]] auto Imgui_windows::get_mutex() -> std::mutex&
{
    return m_mutex;
}

[[nodiscard]] auto Imgui_windows::get_window_viewport() -> std::shared_ptr<Window_imgui_viewport>
{
    return m_window_imgui_viewport;
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

    bool any_mouse_input_sink{false};
    for (const auto& viewport : m_imgui_viewports)
    {
        Scoped_imgui_context imgui_context{*this, *viewport.get()};

        if (viewport->begin_imgui_frame())
        {
            // TODO  menu();
            //// if (viewport == *m_window_imgui_viewport.get())
            //// {
            ////     if (m_show_style_editor)
            ////     {
            ////         if (ImGui::Begin("Dear ImGui Style Editor", &m_show_style_editor))
            ////         {
            ////             ImGui::ShowStyleEditor();
            ////         }
            ////         ImGui::End();
            ////     }
            //// }

            std::size_t i = 0;
            for (auto& imgui_window : m_imgui_windows)
            {
                if (imgui_window->get_viewport() != viewport.get())
                {
                    continue;
                }
                if (imgui_window->is_visible())
                {
                    auto imgui_id = fmt::format("##window-{}", ++i);
                    ImGui::PushID(imgui_id.c_str());
                    if (imgui_window->begin())
                    {
                        imgui_window->imgui();
                    }
                    if (
                        imgui_window->consumes_mouse_input() &&
                        ImGui::IsWindowHovered()
                    )
                    {
                        any_mouse_input_sink = true;
                        m_view->set_mouse_input_sink(imgui_window);
                    }
                    imgui_window->end();
                    ImGui::PopID();
                }
            }

            viewport->end_imgui_frame();
        }
    }

    if (!any_mouse_input_sink)
    {
        m_view->set_mouse_input_sink(nullptr);
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
        ImGui::MenuItem("ImGui Style Editor", "", &m_show_style_editor);

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

[[nodiscard]] auto Imgui_windows::want_capture_mouse() const -> bool
{
    return m_window_imgui_viewport->want_capture_mouse();
}

void Imgui_windows::on_focus(int focused)
{
    Scoped_imgui_context scoped_imgui_context{*this, *m_window_imgui_viewport.get()};

    m_window_imgui_viewport->on_focus(focused);
}

void Imgui_windows::on_cursor_enter(int entered)
{
    Scoped_imgui_context scoped_imgui_context{*this, *m_window_imgui_viewport.get()};

    m_window_imgui_viewport->on_cursor_enter(entered);
}

void Imgui_windows::on_mouse_move(
    const double x,
    const double y
)
{
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
