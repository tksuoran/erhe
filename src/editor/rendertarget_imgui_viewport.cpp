// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "rendertarget_imgui_viewport.hpp"
#include "editor_log.hpp"
#include "rendertarget_node.hpp"
#include "windows/viewport_window.hpp"

#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/scoped_imgui_context.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"
#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/texture.hpp"

#include <GLFW/glfw3.h> // TODO Fix dependency ?

#include <imgui.h>
#include <imgui_internal.h>

namespace editor
{

Rendertarget_imgui_viewport::Rendertarget_imgui_viewport(
    Rendertarget_node*                  rendertarget_node,
    const std::string_view              name,
    const erhe::components::Components& components
)
    : erhe::application::Imgui_viewport{
        name,
        components.get<erhe::application::Imgui_renderer>()->get_font_atlas()
    }
    , m_rendertarget_node{rendertarget_node}
    , m_view             {components.get<erhe::application::View>()}
    , m_name             {name}
{
    m_imgui_renderer = components.get<erhe::application::Imgui_renderer>();
    m_imgui_windows  = components.get<erhe::application::Imgui_windows>();

    m_imgui_ini_path = fmt::format("imgui_{}.ini", name);

    m_imgui_renderer->use_as_backend_renderer_on_context(m_imgui_context);

    ImGuiIO& io = m_imgui_context->IO;
    io.MouseDrawCursor = true;
    io.IniFilename = m_imgui_ini_path.c_str();

    IM_ASSERT(io.BackendPlatformUserData == NULL && "Already initialized a platform backend!");

    io.BackendPlatformUserData = this;
    io.BackendPlatformName     = "erhe rendertarget";
    io.DisplaySize             = ImVec2{static_cast<float>(m_rendertarget_node->width()), static_cast<float>(m_rendertarget_node->height())};
    io.DisplayFramebufferScale = ImVec2{1.0f, 1.0f};

    io.MousePos                = ImVec2{-FLT_MAX, -FLT_MAX};
    io.MouseHoveredViewport    = 0;

    m_last_mouse_x = -FLT_MAX;
    m_last_mouse_y = -FLT_MAX;

    ImGui::SetCurrentContext(nullptr);
}

Rendertarget_imgui_viewport::~Rendertarget_imgui_viewport()
{
    ImGui::DestroyContext(m_imgui_context);
}

template <typename T>
[[nodiscard]] inline auto as_span(const T& value) -> gsl::span<const T>
{
    return gsl::span<const T>(&value, 1);
}

[[nodiscard]] auto Rendertarget_imgui_viewport::rendertarget_node() -> Rendertarget_node*
{
    return m_rendertarget_node;
}

[[nodiscard]] auto Rendertarget_imgui_viewport::begin_imgui_frame() -> bool
{
    SPDLOG_LOGGER_TRACE(log_rendertarget_imgui_windows, "Rendertarget_imgui_viewport::begin_imgui_frame()");

    const auto pointer = m_rendertarget_node->get_pointer();
    if (pointer.has_value())
    {
        if (!has_cursor())
        {
            on_cursor_enter(1);
        }
        const auto position = pointer.value();
        if (
            (m_last_mouse_x != position.x) ||
            (m_last_mouse_y != position.y)
        )
        {
            m_last_mouse_x = position.x;
            m_last_mouse_y = position.y;
            on_mouse_move(position.x, position.y);
        }
    }
    else
    {
        if (has_cursor())
        {
            on_cursor_enter(0);
            m_last_mouse_x = -FLT_MAX;
            m_last_mouse_y = -FLT_MAX;
            on_mouse_move(-FLT_MAX, -FLT_MAX);
        }
    }

    const auto current_time = glfwGetTime();
    ImGuiIO& io = m_imgui_context->IO;
    io.DeltaTime = m_time > 0.0
        ? static_cast<float>(current_time - m_time)
        : static_cast<float>(1.0 / 60.0);
    m_time = current_time;

    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::PushFont(m_imgui_renderer->vr_primary_font());

    // TODO
    //// ImGui::ShowDemoWindow();
    //// ImGui::ShowMetricsWindow();
    //// ImGui::ShowStackToolWindow();

    return true;
}

void Rendertarget_imgui_viewport::end_imgui_frame()
{
    SPDLOG_LOGGER_TRACE(log_rendertarget_imgui_windows, "Rendertarget_imgui_viewport::end_imgui_frame()");

    ImGui::PopFont();
    ImGui::EndFrame();
    ImGui::Render();
}

void Rendertarget_imgui_viewport::execute_render_graph_node()
{
    SPDLOG_LOGGER_TRACE(log_rendertarget_imgui_windows, "Rendertarget_imgui_viewport::execute_render_graph_node()");

    erhe::application::Imgui_windows&  imgui_windows  = *m_imgui_windows.get();
    erhe::application::Imgui_viewport& imgui_viewport = *this;

    erhe::application::Scoped_imgui_context imgui_context(imgui_windows, imgui_viewport);

    m_rendertarget_node->bind();
    m_rendertarget_node->clear(glm::vec4{0.0f, 0.0f, 0.0f, 0.8f});
    m_imgui_renderer->render_draw_data();
    m_rendertarget_node->render_done();
}

}  // namespace editor
