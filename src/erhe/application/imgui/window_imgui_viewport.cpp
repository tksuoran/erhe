// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/application/imgui/window_imgui_viewport.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/application/application_view.hpp"
#include "erhe/application/time.hpp"
#include "erhe/application/window.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/application/imgui/scoped_imgui_context.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/application/windows/pipelines.hpp"
#include "erhe/components/components.hpp"

#include "erhe/geometry/shapes/regular_polygon.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"

#include <GLFW/glfw3.h>

#include <gsl/gsl>

#include <imgui.h>
#include <imgui_internal.h>

namespace erhe::application
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

Window_imgui_viewport::Window_imgui_viewport(
    const std::string_view name
)
    : Imgui_viewport{
        name,
        true,
        g_imgui_renderer->get_font_atlas()
    }
{
    g_imgui_renderer->use_as_backend_renderer_on_context(m_imgui_context);

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

auto Window_imgui_viewport::begin_imgui_frame() -> bool
{
    SPDLOG_LOGGER_TRACE(log_frame, "Window_imgui_viewport::begin_imgui_frame()");

    ERHE_PROFILE_FUNCTION

    const auto& context_window = g_window->get_context_window();
    auto*       glfw_window    = context_window->get_glfw_window();

    const auto w         = g_view->width();
    const auto h         = g_view->height();
    const bool visible   = glfwGetWindowAttrib(glfw_window, GLFW_VISIBLE  ) == GLFW_TRUE;
    const bool iconified = glfwGetWindowAttrib(glfw_window, GLFW_ICONIFIED) == GLFW_TRUE;

    if (
        (w < 1) ||
        (h < 1) ||
        !visible ||
        iconified
    )
    {
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

    // ImGui_ImplGlfw_UpdateMousePosAndButtons();
    io.MousePos = ImVec2{-FLT_MAX, -FLT_MAX};
    if (m_has_cursor)
    {
        double mouse_x{0.0};
        double mouse_y{0.0};
        glfwGetCursorPos(glfw_window, &mouse_x, &mouse_y);
        io.MousePos = ImVec2{
            static_cast<float>(mouse_x),
            static_cast<float>(mouse_y)
        };
    }

    // ImGui_ImplGlfw_UpdateMouseCursor
    const auto cursor = static_cast<erhe::toolkit::Mouse_cursor>(ImGui::GetMouseCursor());
    context_window->set_cursor(cursor);

    SPDLOG_LOGGER_TRACE(log_frame, "ImGui::NewFrame()");
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

    menu();
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
    Scoped_imgui_context imgui_context{*this};

    const auto& context_window = g_window->get_context_window();
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    gl::viewport        (0, 0, context_window->get_width(), context_window->get_height());
    gl::clear_color     (0.0f, 0.0f, 0.0f, 0.0f);
    gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);
    g_imgui_renderer->render_draw_data();
}

auto Window_imgui_viewport::get_producer_output_viewport(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> erhe::scene::Viewport
{
    static_cast<void>(resource_routing); // TODO Validate
    static_cast<void>(key); // TODO Validate
    static_cast<void>(depth);
    return erhe::scene::Viewport{
        .x             = 0,
        .y             = 0,
        .width         = g_window->get_context_window()->get_width(),
        .height        = g_window->get_context_window()->get_height(),
        // .reverse_depth = false // unused
    };
}

}  // namespace editor
