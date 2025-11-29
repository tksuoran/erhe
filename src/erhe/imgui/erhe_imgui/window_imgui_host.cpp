// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_imgui/window_imgui_host.hpp"
#include "erhe_imgui/imgui_log.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/scoped_imgui_context.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_window/window.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace erhe::imgui {

using erhe::graphics::Render_pass;
using erhe::graphics::Texture;

Window_imgui_host::Window_imgui_host(
    Imgui_renderer&                 imgui_renderer,
    erhe::graphics::Device&         graphics_device,
    erhe::graphics::Swapchain&      swapchain,
    erhe::rendergraph::Rendergraph& rendergraph,
    erhe::window::Context_window&   context_window,
    const std::string_view          name
)
    : Imgui_host       {rendergraph, imgui_renderer, name, true, imgui_renderer.get_font_atlas()}
    , m_context_window {context_window}
    , m_graphics_device{graphics_device}
    , m_swapchain      {swapchain}
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

    const int width  = m_context_window.get_width();
    const int height = m_context_window.get_height();
    update_render_pass(width, height);
}

void Window_imgui_host::update_render_pass(int width, int height)
{
    if (
        m_render_pass &&
        (m_render_pass->get_render_target_width () == width) &&
        (m_render_pass->get_render_target_height() == height)
    )
    {
        return;
    }

    m_render_pass.reset();
    erhe::graphics::Render_pass_descriptor render_pass_descriptor;
    render_pass_descriptor.swapchain = &m_swapchain;
    render_pass_descriptor.color_attachments[0].load_action    = erhe::graphics::Load_action::Clear;
    render_pass_descriptor.color_attachments[0].clear_value[0] = 0.05; // TODO expose API to set clear color
    render_pass_descriptor.color_attachments[0].clear_value[1] = 0.05;
    render_pass_descriptor.color_attachments[0].clear_value[2] = 0.05;
    render_pass_descriptor.color_attachments[0].clear_value[3] = 1.00;
    render_pass_descriptor.depth_attachment    .load_action    = erhe::graphics::Load_action::Dont_care;
    render_pass_descriptor.stencil_attachment  .load_action    = erhe::graphics::Load_action::Dont_care;
    render_pass_descriptor.render_target_width  = width;
    render_pass_descriptor.render_target_height = height;
    render_pass_descriptor.debug_label          = "Window_imgui_host Render_pass";
    m_render_pass = std::make_unique<erhe::graphics::Render_pass>(m_graphics_device, render_pass_descriptor);
}

Window_imgui_host::~Window_imgui_host()
{
}

void Window_imgui_host::process_events(const float dt_s, const int64_t time_ns)
{
    //SPDLOG_LOGGER_TRACE(log_frame, "Window_imgui_host::begin_imgui_frame()");

    ERHE_PROFILE_FUNCTION();

    m_this_frame_dt_s += dt_s;
    static_cast<void>(time_ns); // TODO filter event processing based on time

    // log_frame->info("Window_imgui_host::process_input_events_from_context_window()");

    // Process input events from the context window
    std::vector<erhe::window::Input_event>& input_events = m_context_window.get_input_events();
    if (input_events.empty()) {
        SPDLOG_LOGGER_TRACE(log_frame, "Window_imgui_host - no input events");
    }
    for (erhe::window::Input_event& input_event : input_events) {
        //// if (input_event.timestamp_ns > time_ns) {
        ////     break;
        //// }
        if (!input_event.handled) {
            dispatch_input_event(input_event);
            SPDLOG_LOGGER_TRACE(log_frame, "Window_imgui_host handled {}", input_event.describe());
            SPDLOG_LOGGER_TRACE(log_input_events, "Window_imgui_host handled {}", input_event.describe());
        } else {
            SPDLOG_LOGGER_TRACE(log_input_events, "Window_imgui_host skipped already handled {}", input_event.describe());
        }
    }
}

void Window_imgui_host::begin_imgui_frame()
{
    //SPDLOG_LOGGER_TRACE(log_frame, "Window_imgui_host::begin_imgui_frame()");

    ERHE_PROFILE_FUNCTION();

    /// auto* glfw_window    = m_context_window.get_glfw_window();

    const int  width     = m_context_window.get_width();
    const int  height    = m_context_window.get_height();
    const bool visible   = true;  //// TODO glfwGetWindowAttrib(glfw_window, GLFW_VISIBLE  ) == GLFW_TRUE;
    const bool iconified = false; ////      glfwGetWindowAttrib(glfw_window, GLFW_ICONIFIED) == GLFW_TRUE;

    if ((width < 1) || (height < 1) || !visible || iconified) {
        m_is_visible = false;
        return;
    }
    m_is_visible = true;
    update_render_pass(width, height);

    ImGuiIO& io = m_imgui_context->IO;
    io.DisplaySize = ImVec2{static_cast<float>(width), static_cast<float>(height)};
    io.DeltaTime   = m_this_frame_dt_s > 0.0f ? m_this_frame_dt_s : static_cast<float>(1.0 / 60.0);

    // ImGui_ImplGlfw_UpdateMouseCursor
    const auto cursor = static_cast<erhe::window::Mouse_cursor>(ImGui::GetMouseCursor());
    m_context_window.set_cursor(cursor);

    SPDLOG_LOGGER_TRACE(log_frame, "ImGui::NewFrame()");
    ImGui::NewFrame();

    ImFont* font = m_imgui_renderer.primary_font();
    ImGui::PushFont(font, m_imgui_renderer.get_imgui_settings().font_size);

    const float status_bar_height = ImGui::GetFrameHeight();

    // Root window
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDocking             |
        ImGuiWindowFlags_NoTitleBar            |
        ImGuiWindowFlags_NoCollapse            |
        ImGuiWindowFlags_NoScrollbar           |
        ImGuiWindowFlags_NoResize              |
        ImGuiWindowFlags_NoMove                |
        ImGuiWindowFlags_NoBringToFrontOnFocus | 
        ImGuiWindowFlags_NoNavFocus            |
        ImGuiWindowFlags_MenuBar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("erhe root window", nullptr, window_flags);
    ImGui::PopStyleVar(2);

    // Menu
    if (m_begin_callback) {
        m_begin_callback(*this);
    }

    // Dockspace
    ImGui::DockSpace(ImGui::GetID(0), ImVec2{0.0f, -status_bar_height}, ImGuiDockNodeFlags_PassthruCentralNode);

    // Status bar
    ImGui::SetCursorPos(ImVec2{0.0f, viewport->Size.y - status_bar_height});
    ImGui::BeginChild("StatusBar", ImVec2{viewport->Size.x, status_bar_height}, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (m_status_bar_callback) {
        m_status_bar_callback(*this);
    }
    ImGui::EndChild();

    ImGui::End();
}

void Window_imgui_host::end_imgui_frame()
{
    SPDLOG_LOGGER_TRACE(log_frame, "Window_imgui_host::end_imgui_frame()");
    ImGui::PopFont();
    ImGui::EndFrame();
    ImGui::Render();
    m_this_frame_dt_s = 0.0f;
}

auto Window_imgui_host::is_visible() const -> bool
{
    return m_is_visible;
}

void Window_imgui_host::set_text_input_area(int x, int y, int w, int h)
{
    m_context_window.set_text_input_area(x, y, w, h);
}

void Window_imgui_host::start_text_input()
{
    m_context_window.start_text_input();
}

void Window_imgui_host::stop_text_input()
{
    m_context_window.stop_text_input();
}

void Window_imgui_host::execute_rendergraph_node()
{
    ERHE_PROFILE_FUNCTION();

    Scoped_imgui_context imgui_context{*this};

    erhe::graphics::Render_command_encoder render_encoder = m_graphics_device.make_render_command_encoder(*m_render_pass.get());
    m_imgui_renderer.render_draw_data(render_encoder);
}

auto Window_imgui_host::get_viewport() const -> erhe::math::Viewport
{
    return erhe::math::Viewport{
        .x      = 0,
        .y      = 0,
        .width  = m_context_window.get_width(),
        .height = m_context_window.get_height()
    };
}

void Window_imgui_host::set_status_bar_callback(const std::function<void(Window_imgui_host& host)>& callback)
{
    m_status_bar_callback = callback;
}

}  // namespace erhe::imgui
