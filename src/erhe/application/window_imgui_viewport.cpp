#include "erhe/application/window_imgui_viewport.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui_helpers.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/time.hpp"
#include "erhe/application/window.hpp"

#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"
#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/application/windows/pipelines.hpp"

#include "erhe/geometry/shapes/regular_polygon.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"

#include <GLFW/glfw3.h>

#include <gsl/gsl>

namespace erhe::application
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

Window_imgui_viewport::Window_imgui_viewport(
    const std::shared_ptr<Imgui_renderer>& imgui_renderer
)
    : m_imgui_renderer{imgui_renderer}
{
    auto* font_atlas = m_imgui_renderer->get_font_atlas();
    m_imgui_context = ImGui::CreateContext(font_atlas);

    m_imgui_renderer->use_as_backend_renderer_on_context(m_imgui_context);

    ImGuiIO& io     = ImGui::GetIO(m_imgui_context);
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

void Window_imgui_viewport::post_initialize(
    Imgui_windows*                 imgui_windows,
    const std::shared_ptr<View  >& view,
    const std::shared_ptr<Window>& window
)
{
    m_imgui_windows = imgui_windows;
    m_view          = view;
    m_window        = window;
}

void Window_imgui_viewport::begin_imgui_frame()
{
    SPDLOG_LOGGER_TRACE(log_frame, "Window_imgui_viewport::begin_imgui_frame()");

    ERHE_PROFILE_FUNCTION

    const auto& context_window = m_window->get_context_window();
    auto*       glfw_window    = context_window->get_glfw_window();

    const auto w = m_view->width();
    const auto h = m_view->height();

    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
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
    for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++)
    {
        io.MouseDown[i] = m_mouse_just_pressed[i] || glfwGetMouseButton(glfw_window, i) != 0;
        m_mouse_just_pressed[i] = false;
        ///// for (const auto& rendertarget : m_rendertarget_imgui_windows)
        ///// {
        /////     rendertarget->mouse_button(i, io.MouseDown[i]);
        ///// }
    }
    if (m_has_cursor)
    {
        double mouse_x;
        double mouse_y;
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
}

void Window_imgui_viewport::end_imgui_frame()
{
}

void Window_imgui_viewport::render_imgui_frame()
{
    ERHE_PROFILE_FUNCTION

    SPDLOG_LOGGER_TRACE(log_frame, "ImGui::Render()");
    ImGui::Render();

    m_imgui_renderer->render_draw_data();
}

void Window_imgui_viewport::menu()
{
    ERHE_VERIFY(m_imgui_windows != nullptr);

    if (ImGui::BeginMainMenuBar())
    {
        m_imgui_windows->window_menu();
        ImGui::EndMainMenuBar();
    }
}

}  // namespace editor
