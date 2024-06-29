// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_imgui/window_imgui_viewport.hpp"
#include "erhe_imgui/imgui_log.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/scoped_imgui_context.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_window/window.hpp"
#include "erhe_profile/profile.hpp"

#include <GLFW/glfw3.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace erhe::imgui
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

Window_imgui_viewport::Window_imgui_viewport(
    Imgui_renderer&                 imgui_renderer,
    erhe::window::Context_window&   context_window,
    erhe::rendergraph::Rendergraph& rendergraph,
    const std::string_view          name
)
    : Imgui_viewport{
        rendergraph,
        imgui_renderer,
        name,
        true,
        imgui_renderer.get_font_atlas()
    }
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

    //bool window_viewport   = true;
    //// {
    ////     auto ini = erhe::configuration::get_ini("erhe.ini", "imgui");
    ////     ini->get("window_viewport", window_viewport);
    //// }
    ////
    //// if (window_viewport) {
    ////     g_imgui_windows->register_context_window(
    ////         get_context_window()
    ////     );
    //// }

}

Window_imgui_viewport::~Window_imgui_viewport()
{
}

auto Window_imgui_viewport::begin_imgui_frame() -> bool
{
    SPDLOG_LOGGER_TRACE(log_frame, "Window_imgui_viewport::begin_imgui_frame()");

    ERHE_PROFILE_FUNCTION();

    auto* glfw_window    = m_context_window.get_glfw_window();

    const auto w         = m_context_window.get_width();
    const auto h         = m_context_window.get_height();
    const bool visible   = glfwGetWindowAttrib(glfw_window, GLFW_VISIBLE  ) == GLFW_TRUE;
    const bool iconified = glfwGetWindowAttrib(glfw_window, GLFW_ICONIFIED) == GLFW_TRUE;

    if (
        (w < 1) ||
        (h < 1) ||
        !visible ||
        iconified
    ) {
        return false;
    }

    ImGuiIO& io = m_imgui_context->IO;
    io.DisplaySize = ImVec2{
        static_cast<float>(w),
        static_cast<float>(h)
    };

    // Setup time step
    const auto current_time = glfwGetTime();
    io.DeltaTime = m_time > 0.0
        ? static_cast<float>(current_time - m_time)
        : static_cast<float>(1.0 / 60.0);
    m_time = current_time;

    // ImGui_ImplGlfw_UpdateMouseCursor
    const auto cursor = static_cast<erhe::window::Mouse_cursor>(ImGui::GetMouseCursor());
    m_context_window.set_cursor(cursor);

    SPDLOG_LOGGER_TRACE(log_frame, "ImGui::NewFrame()");
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

    if (m_begin_callback) {
        m_begin_callback(*this);
    }

    flush_queud_events();

    return true;
}

void Window_imgui_viewport::end_imgui_frame()
{
    SPDLOG_LOGGER_TRACE(log_frame, "Window_imgui_viewport::end_imgui_frame()");
    ImGui::EndFrame();
    ImGui::Render();
}

void Window_imgui_viewport::execute_rendergraph_node()
{
    ERHE_PROFILE_FUNCTION();

    Scoped_imgui_context imgui_context{*this};

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    gl::viewport        (0, 0, m_context_window.get_width(), m_context_window.get_height());
    gl::clear_color     (0.0f, 0.0f, 0.0f, 0.0f);
    gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);
    m_imgui_renderer.render_draw_data();
}

auto Window_imgui_viewport::get_producer_output_viewport(
    const erhe::rendergraph::Resource_routing resource_routing,
    const int                                 key,
    const int                                 depth
) const -> erhe::math::Viewport
{
    static_cast<void>(resource_routing); // TODO Validate
    static_cast<void>(key); // TODO Validate
    static_cast<void>(depth);
    return erhe::math::Viewport{
        .x      = 0,
        .y      = 0,
        .width  = m_context_window.get_width(),
        .height = m_context_window.get_height(),
        // .reverse_depth = false // unused
    };
}

}  // namespace erhe::imgui
