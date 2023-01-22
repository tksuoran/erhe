// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/application/application_view.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/application/time.hpp"
#include "erhe/application/window.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"

#include "erhe/gl/enum_bit_mask_operators.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/raytrace/mesh_intersect.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <backends/imgui_impl_glfw.h>
#endif

namespace erhe::application {

View* g_view{nullptr};

View::View()
    : erhe::components::Component{c_type_name}
{
}

View::~View() noexcept
{
    ERHE_VERIFY(g_view == this);
    g_view = nullptr;
}

void View::declare_required_components()
{
    require<Rendergraph>();
    require<Window     >();
}

void View::initialize_component()
{
    ERHE_VERIFY(g_view == nullptr);
    g_view = this;
}

void View::set_client(View_client* view_client)
{
    m_view_client = view_client;
}

void View::on_refresh()
{
    if (!g_configuration->window.show)
    {
        return;
    }
    if (!m_ready)
    {
        gl::clear_color(0.3f, 0.3f, 0.3f, 0.4f);
        gl::clear(gl::Clear_buffer_mask::color_buffer_bit);
        g_window->get_context_window()->swap_buffers();
        return;
    }

    if (
        (m_view_client != nullptr) &&
        g_configuration->window.show
    )
    {
        if (g_time != nullptr)
        {
            g_time->update(); // Does not do once per frame updates - moving to next slot in renderers
        }
        m_view_client->update(); // Should call once per frame updates
        if (g_window != nullptr)
        {
            g_window->get_context_window()->swap_buffers();
        }
    }
}

static constexpr std::string_view c_swap_buffers{"swap_buffers"};

void View::run()
{
    //m_imgui_windows->make_imgui_context_current();
    for (;;)
    {
        SPDLOG_LOGGER_TRACE(log_frame, "\n-------- new frame --------\n");

        if (m_close_requested)
        {
            log_frame->info("close was requested, exiting loop");
            break;
        }

        {
            SPDLOG_LOGGER_TRACE(log_frame, "> before poll events()");
            g_window->get_context_window()->poll_events();
            SPDLOG_LOGGER_TRACE(log_frame, "> after poll events()");
        }

        if (m_close_requested)
        {
            log_frame->info("close was requested, exiting loop");
            break;
        }

        update();
    }
}

void View::on_close()
{
    SPDLOG_LOGGER_TRACE(log_frame, "on_close()");

    m_close_requested = true;
}

void View::update()
{
    ERHE_PROFILE_FUNCTION

    SPDLOG_LOGGER_TRACE(log_frame, "update()");

    if (g_time != nullptr)
    {
        g_time->update();
    }

    if (m_view_client != nullptr)
    {
        m_view_client->update();
    }
    else if (g_time != nullptr)
    {
        g_time->update_once_per_frame();
    }

    if (g_configuration->window.show)
    {
        ERHE_PROFILE_SCOPE(c_swap_buffers.data());

        erhe::graphics::Gpu_timer::end_frame();
        g_window->get_context_window()->swap_buffers();
        if (g_configuration->window.use_finish)
        {
            gl::finish();
        }
    }

    m_ready = true;
}

[[nodiscard]] auto View::view_client() const -> View_client*
{
    return m_view_client;
}

void View::on_enter()
{
    if (g_time != nullptr)
    {
        g_time->start_time();
    }
}

void View::on_focus(int focused)
{
    if (g_imgui_windows != nullptr)
    {
       g_imgui_windows->on_focus(focused);
    }
}

void View::on_cursor_enter(int entered)
{
    if (g_imgui_windows != nullptr)
    {
        g_imgui_windows->on_cursor_enter(entered);
    }
}

void View::on_key(
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask,
    const bool                   pressed
)
{
    if (g_imgui_windows != nullptr)
    {
        g_imgui_windows->on_key(
            static_cast<signed int>(code),
            modifier_mask,
            pressed
        );
    }

    if (g_commands == nullptr)
    {
        return;
    }

    if (get_imgui_capture_keyboard())
    {
        return;
    }

    if (m_view_client != nullptr)
    {
        m_view_client->update_keyboard(pressed, code, modifier_mask);
    }

    g_commands->on_key(code, modifier_mask, pressed);
}

void View::on_char(
    const unsigned int codepoint
)
{
    log_input_event->trace("char input codepoint = {}", codepoint);
    if (g_imgui_windows != nullptr)
    {
        g_imgui_windows->on_char(codepoint);
    }
}

auto View::get_imgui_capture_keyboard() const -> bool
{
    const bool viewports_hosted_in_imgui =
        g_configuration->window.show &&
        g_configuration->imgui.window_viewport;

    if (!viewports_hosted_in_imgui)
    {
        return false;
    }

    if (g_imgui_windows == nullptr)
    {
        return false;
    }

    return g_imgui_windows->want_capture_keyboard();
}

auto View::get_imgui_capture_mouse() const -> bool
{
    const bool viewports_hosted_in_imgui =
        g_configuration->window.show &&
        g_configuration->imgui.window_viewport;

    if (!viewports_hosted_in_imgui)
    {
        return false;
    }

    if (g_imgui_windows == nullptr)
    {
        return false;
    }

    return g_imgui_windows->want_capture_mouse();
}

void View::on_mouse_click(
    const erhe::toolkit::Mouse_button button,
    const int                         count
)
{
    if (g_imgui_windows != nullptr)
    {
        g_imgui_windows->on_mouse_click(static_cast<uint32_t>(button), count);
    }

    if (g_commands == nullptr)
    {
        return;
    }

    if (get_imgui_capture_mouse())
    {
        return;
    }

    log_input_event->trace(
        "mouse button {} {}",
        erhe::toolkit::c_str(button),
        count
    );

    if (m_view_client != nullptr)
    {
        m_view_client->update_mouse(button, count);
    }

    g_commands->on_mouse_click(button, count);
}

void View::on_mouse_wheel(const double x, const double y)
{
    if (g_imgui_windows != nullptr)
    {
        g_imgui_windows->on_mouse_wheel(x, y);
    }

    if (g_commands == nullptr)
    {
        return;
    }

    if (get_imgui_capture_mouse())
    {
        return;
    }

    log_input_event->trace("mouse wheel {}, {}", x, y);

    g_commands->on_mouse_wheel(x, y);
}

void View::on_mouse_move(const double x, const double y)
{
    if (g_imgui_windows != nullptr)
    {
        g_imgui_windows->on_mouse_move(x, y);
    }

    if (g_commands == nullptr)
    {
        return;
    }

    if (get_imgui_capture_mouse())
    {
        return;
    }

    if (m_view_client != nullptr)
    {
        m_view_client->update_mouse(x, y);
    }

    g_commands->on_mouse_move(x, y);
}

}  // namespace editor
