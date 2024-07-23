// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_imgui/window_imgui_host.hpp"
#include "erhe_imgui/imgui_log.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/scoped_imgui_context.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_window/window.hpp"
#include "erhe_profile/profile.hpp"

#include <GLFW/glfw3.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace erhe::imgui {

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

Window_imgui_host::Window_imgui_host(
    Imgui_renderer&                 imgui_renderer,
    erhe::window::Context_window&   context_window,
    erhe::rendergraph::Rendergraph& rendergraph,
    const std::string_view          name
)
    : Imgui_host{rendergraph, imgui_renderer, name, true, imgui_renderer.get_font_atlas()}
    , m_context_window{context_window}
{
    imgui_renderer.use_as_backend_renderer_on_context(m_imgui_context);

    ImGuiIO& io     = m_imgui_context->IO;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    io.BackendPlatformUserData = this;
    io.BackendPlatformName = "imgui_impl_erhe";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
    //io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)
    //io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;    // We can create multi-viewports on the Platform side (optional)

    // TODO Clipboard
    // TODO Update monitors

    // Our mouse update function expect PlatformHandle to be filled for the main viewport
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    main_viewport->PlatformHandle = this;

    ImGui::SetCurrentContext(nullptr);

    m_time = 0.0;
}

Window_imgui_host::~Window_imgui_host()
{
}

void Window_imgui_host::process_input_events_from_context_window()
{
    // Process input events from the context window
    std::vector<erhe::window::Input_event>& input_events = m_context_window.get_input_events();
    if (input_events.empty()) {
        SPDLOG_LOGGER_TRACE(log_frame, "Window_imgui_host - no input events");
    }
    for (erhe::window::Input_event& input_event : input_events) {
        if (!input_event.handled) {
            dispatch_input_event(input_event);
            SPDLOG_LOGGER_TRACE(log_frame, "Window_imgui_host handled {}", input_event.describe());
            SPDLOG_LOGGER_TRACE(log_input_events, "Window_imgui_host handled {}", input_event.describe());
        } else {
            SPDLOG_LOGGER_TRACE(log_input_events, "Window_imgui_host skipped already handled {}", input_event.describe());
        }
    }
}

auto Window_imgui_host::begin_imgui_frame() -> bool
{
    //SPDLOG_LOGGER_TRACE(log_frame, "Window_imgui_host::begin_imgui_frame()");

    ERHE_PROFILE_FUNCTION();

#if 1 // TODO Enable only this path permanently
    process_input_events_from_context_window();
#endif

    auto* glfw_window    = m_context_window.get_glfw_window();

    const auto w         = m_context_window.get_width();
    const auto h         = m_context_window.get_height();
    const bool visible   = glfwGetWindowAttrib(glfw_window, GLFW_VISIBLE  ) == GLFW_TRUE;
    const bool iconified = glfwGetWindowAttrib(glfw_window, GLFW_ICONIFIED) == GLFW_TRUE;

    if ((w < 1) || (h < 1) || !visible || iconified) {
        return false;
    }

    ImGuiIO& io = m_imgui_context->IO;
    io.DisplaySize = ImVec2{static_cast<float>(w), static_cast<float>(h)};

    // Setup time step
    const auto current_time = glfwGetTime();
    io.DeltaTime = m_time > 0.0 ? static_cast<float>(current_time - m_time) : static_cast<float>(1.0 / 60.0);
    m_time = current_time;

#if 0 // TODO Temp old path for OpenXR compatibility when Window_imgui_host was used with OpenXR
    process_input_events_from_context_window();
#endif

    // ImGui_ImplGlfw_UpdateMouseCursor
    const auto cursor = static_cast<erhe::window::Mouse_cursor>(ImGui::GetMouseCursor());
    m_context_window.set_cursor(cursor);

    SPDLOG_LOGGER_TRACE(log_frame, "ImGui::NewFrame()");
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

    if (m_begin_callback) {
        m_begin_callback(*this);
    }

    return true;
}

void Window_imgui_host::end_imgui_frame()
{
    SPDLOG_LOGGER_TRACE(log_frame, "Window_imgui_host::end_imgui_frame()");
    ImGui::EndFrame();
    ImGui::Render();
}

void Window_imgui_host::execute_rendergraph_node()
{
    ERHE_PROFILE_FUNCTION();

    Scoped_imgui_context imgui_context{*this};

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    gl::viewport        (0, 0, m_context_window.get_width(), m_context_window.get_height());
    gl::clear_color     (0.0f, 0.0f, 0.0f, 0.0f);
    gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);
    m_imgui_renderer.render_draw_data();
}

auto Window_imgui_host::get_producer_output_viewport(erhe::rendergraph::Routing, int, int) const -> erhe::math::Viewport
{
    return erhe::math::Viewport{
        .x      = 0,
        .y      = 0,
        .width  = m_context_window.get_width(),
        .height = m_context_window.get_height(),
        // .reverse_depth = false // unused
    };
}

}  // namespace erhe::imgui
