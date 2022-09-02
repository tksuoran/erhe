// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/application/view.hpp"

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

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <backends/imgui_impl_glfw.h>
#endif

namespace erhe::application {

View::View()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title, c_type_name}
{
}

View::~View() noexcept
{
}

void View::declare_required_components()
{
    m_imgui_windows = require<Imgui_windows>();
    m_render_graph  = require<Rendergraph  >();
    m_window        = require<Window       >();
}

void View::initialize_component()
{
    m_imgui_windows->register_imgui_window(this);
}

void View::post_initialize()
{
    m_commands       = get<Commands      >();
    m_configuration  = get<Configuration >();
    m_imgui_renderer = get<Imgui_renderer>();
    m_time           = get<Time          >();
}

void View::set_client(View_client* view_client)
{
    m_view_client = view_client;
}

void View::on_refresh()
{
    if (!m_configuration->window.show)
    {
        return;
    }
    if (!m_ready)
    {
        gl::clear_color(0.3f, 0.3f, 0.3f, 0.4f);
        gl::clear(gl::Clear_buffer_mask::color_buffer_bit);
        m_window->get_context_window()->swap_buffers();
        return;
    }
    else
    {
        gl::clear_color(0.0f, 0.0f, 0.0f, 0.4f);
        gl::clear(gl::Clear_buffer_mask::color_buffer_bit);
    }

    // TODO execute render graph?
    m_window->get_context_window()->swap_buffers();
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
            get<Window>()->get_context_window()->poll_events();
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

    m_time->update();

    if (m_view_client != nullptr)
    {
        m_view_client->update();
    }

    if (m_configuration->window.show)
    {
        ERHE_PROFILE_SCOPE(c_swap_buffers.data());

        erhe::graphics::Gpu_timer::end_frame();
        m_window->get_context_window()->swap_buffers();
        if (m_configuration->window.use_finish)
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
    m_time->start_time();
}

void View::on_focus(int focused)
{
    m_imgui_windows->on_focus(focused);
}

void View::on_cursor_enter(int entered)
{
    m_imgui_windows->on_cursor_enter(entered);
}

void View::on_key(
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask,
    const bool                   pressed
)
{
    m_imgui_windows->on_key(
        static_cast<signed int>(code),
        modifier_mask,
        pressed
    );

    if (m_view_client != nullptr)
    {
        m_view_client->update_keyboard(pressed, code, modifier_mask);
    }

    m_commands->on_key(code, modifier_mask, pressed);
}

void View::on_char(
    const unsigned int codepoint
)
{
    log_input_event->trace("char input codepoint = {}", codepoint);
    m_imgui_windows->on_char(codepoint);
}

auto View::get_imgui_capture_mouse() const -> bool
{
    const bool viewports_hosted_in_imgui =
        m_configuration->window.show &&
        m_configuration->imgui.window_viewport;

    if (!viewports_hosted_in_imgui)
    {
        return false;
    }

    const auto& imgui_windows = get<Imgui_windows>();
    if (!imgui_windows)
    {
        return false;
    }

    return imgui_windows->want_capture_mouse();
}

void View::on_mouse_click(
    const erhe::toolkit::Mouse_button button,
    const int                         count
)
{
    m_imgui_windows->on_mouse_click(static_cast<uint32_t>(button), count);

    const bool imgui_capture_mouse  = get_imgui_capture_mouse();
    const bool has_mouse_input_sink = (m_commands->mouse_input_sink() != nullptr);

    if (imgui_capture_mouse && !has_mouse_input_sink)
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

    m_commands->on_mouse_click(button, count);
}

void View::on_mouse_wheel(const double x, const double y)
{
    m_imgui_windows->on_mouse_wheel(x, y);

    const bool imgui_capture_mouse  = get_imgui_capture_mouse();
    const bool has_mouse_input_sink = (m_commands->mouse_input_sink() != nullptr);

    if (imgui_capture_mouse && !has_mouse_input_sink)
    {
        return;
    }

    log_input_event->trace("mouse wheel {}, {}", x, y);

    m_commands->on_mouse_wheel(x, y);
}

void View::on_mouse_move(const double x, const double y)
{
    m_imgui_windows->on_mouse_move(x, y);

    const bool imgui_capture_mouse  = get_imgui_capture_mouse();
    const bool has_mouse_input_sink = (m_commands->mouse_input_sink() != nullptr);

    if (imgui_capture_mouse && !has_mouse_input_sink)
    {
        SPDLOG_LOGGER_TRACE(log_input_event_filtered, "ImGui WantCaptureMouse");
        return;
    }

    if (m_view_client != nullptr)
    {
        m_view_client->update_mouse(x, y);
    }

    m_commands->on_mouse_move(x, y);
}

void View::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI) && 0
    std::lock_guard<std::mutex> lock{m_command_mutex};

    const ImGuiTreeNodeFlags leaf_flags{
        ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_Leaf
    };

    ImGui::Text(
        "Active mouse command: %s",
        (m_active_mouse_command != nullptr)
            ? m_active_mouse_command->name()
            : "(none)"
    );

    if (ImGui::TreeNodeEx("Active", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto* command : m_commands)
        {
            if (command->state() == State::Active)
            {
                ImGui::TreeNodeEx(command->name(), leaf_flags);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Ready", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto* command : m_commands)
        {
            if (command->state() == State::Ready)
            {
                ImGui::TreeNodeEx(command->name(), leaf_flags);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Inactive", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto* command : m_commands)
        {
            if (command->state() == State::Inactive)
            {
                ImGui::TreeNodeEx(command->name(), leaf_flags);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Disabled", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto* command : m_commands)
        {
            if (command->state() == State::Disabled)
            {
                ImGui::TreeNodeEx(command->name(), leaf_flags);
            }
        }
        ImGui::TreePop();
    }
#endif
}

}  // namespace editor
