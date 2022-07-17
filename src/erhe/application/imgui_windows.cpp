#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/log.hpp"
#include "erhe/application/imgui_viewport.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/window.hpp"
#include "erhe/application/window_imgui_viewport.hpp"
#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/toolkit/profile.hpp"

namespace erhe::application
{

Imgui_windows::Imgui_windows()
    : erhe::components::Component{c_label}
{
}

Imgui_windows::~Imgui_windows() noexcept
{
}

void Imgui_windows::declare_required_components()
{
    require<Imgui_renderer>();
}

void Imgui_windows::initialize_component()
{
    m_imgui_renderer = get<Imgui_renderer>();

    m_window_imgui_viewport = std::make_shared<Window_imgui_viewport>(
        m_imgui_renderer
    );
    m_window_imgui_viewport->make_imgui_context_current();

    m_imgui_viewports.emplace_back(m_window_imgui_viewport);

    m_current_viewport = m_window_imgui_viewport.get();
}

void Imgui_windows::post_initialize()
{
    m_view = get<View>();

    m_window_imgui_viewport->post_initialize(
        this,
        m_view,
        get<Window>()
    );
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

void Imgui_windows::render_imgui_frame()
{
    m_window_imgui_viewport->render_imgui_frame();
}

void Imgui_windows::imgui_windows()
{
    ERHE_PROFILE_FUNCTION

    //Scoped_imgui_context scoped_context{m_imgui_context};

    bool any_mouse_input_sink{false};
    for (const auto& viewport : m_imgui_viewports)
    {
        viewport->begin_imgui_frame();

        // TODO  menu();
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

    if (!any_mouse_input_sink)
    {
        m_view->set_mouse_input_sink(nullptr);
    }

    if (m_show_style_editor)
    {
        if (ImGui::Begin("Dear ImGui Style Editor", &m_show_style_editor))
        {
            ImGui::ShowStyleEditor();
        }
        ImGui::End();
    }
}

void Imgui_windows::window_menu()
{
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
    if (m_current_viewport == nullptr)
    {
        return false;
    }
    return m_current_viewport->want_capture_mouse();
}

void Imgui_windows::on_focus(int focused)
{
    if (m_current_viewport == nullptr)
    {
        return;
    }
    m_current_viewport->on_focus(focused);
}

void Imgui_windows::on_cursor_enter(int entered)
{
    if (m_current_viewport == nullptr)
    {
        return;
    }
    m_current_viewport->on_cursor_enter(entered);
}

void Imgui_windows::on_mouse_click(
    const uint32_t button,
    const int      count
)
{
    if (m_current_viewport == nullptr)
    {
        return;
    }
    m_current_viewport->on_mouse_click(button, count);
}

void Imgui_windows::on_mouse_wheel(
    const double x,
    const double y
)
{
    if (m_current_viewport == nullptr)
    {
        return;
    }
    m_current_viewport->on_mouse_wheel(x, y);
}

void Imgui_windows::on_key(
    const signed int keycode,
    const uint32_t   modifier_mask,
    const bool       pressed
)
{
    if (m_current_viewport == nullptr)
    {
        return;
    }
    m_current_viewport->on_key(keycode, modifier_mask, pressed);
}

void Imgui_windows::on_char(
    const unsigned int codepoint
)
{
    if (m_current_viewport == nullptr)
    {
        return;
    }
    m_current_viewport->on_char(codepoint);
}

}  // namespace editor
