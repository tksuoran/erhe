#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"
#include "erhe/components/components.hpp"
#include "erhe/toolkit/profile.hpp"

#include <imgui.h>

namespace erhe::application {

Imgui_window::Imgui_window(const std::string_view title)
    : m_title{title}
    , m_label{title}
{
}

Imgui_window::Imgui_window(
    const std::string_view title,
    const std::string_view label
)
    : m_title{title}
    , m_label{label}
{
}

Imgui_window::~Imgui_window() noexcept
{
}

void Imgui_window::initialize(
    const erhe::components::Components& components
)
{
    m_imgui_renderer = components.get<Imgui_renderer>();
}

void Imgui_window::image(
    const std::shared_ptr<erhe::graphics::Texture>& texture,
    const int                                       width,
    const int                                       height
)
{
    m_imgui_renderer->image(texture, width, height);
}

void Imgui_window::show()
{
    m_is_visible = true;
}

void Imgui_window::hide()
{
    m_is_visible = false;
}

void Imgui_window::toggle_visibility()
{
    m_is_visible = !m_is_visible;
}

auto Imgui_window::is_visible() const -> bool
{
    return m_is_visible;
}

auto Imgui_window::title() const -> const std::string_view
{
    return m_title;
}

auto Imgui_window::label() -> const char*
{
    return m_label.c_str();
}

auto Imgui_window::begin() -> bool
{
    on_begin();
    bool keep_visible{true};
    ImGui::SetNextWindowSizeConstraints(
        ImVec2{m_min_size},
        ImVec2{m_max_size}
    );
    const bool not_collapsed = ImGui::Begin(
        label(),
        &keep_visible,
        flags()
    );
    if (!keep_visible)
    {
        hide();
    }
    return not_collapsed;
}

void Imgui_window::end()
{
    on_end();
    ImGui::End();
}

auto Imgui_window::flags() -> ImGuiWindowFlags
{
    return 0; //ImGuiWindowFlags_NoCollapse;
}

auto Imgui_window::consumes_mouse_input() const -> bool
{
    return false;
}

auto Imgui_window::get_window_type_hash() const -> uint32_t
{
    return 0;
}

void Imgui_window::on_begin()
{
}

void Imgui_window::on_end()
{
}

Rendertarget_imgui_window::Rendertarget_imgui_window(
    const std::string_view title
)
    : Imgui_window{title}
{
}

auto Rendertarget_imgui_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize;
}

void Rendertarget_imgui_window::on_begin()
{
    ERHE_PROFILE_FUNCTION

#ifdef IMGUI_HAS_VIEWPORT
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
#else
    ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
#endif
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
}

void Rendertarget_imgui_window::on_end()
{
    ImGui::PopStyleVar();
}

} // namespace erhe::application
