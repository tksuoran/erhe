#include "erhe_imgui/windows/framebuffer_window.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_gl/command_info.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace erhe::imgui {

using erhe::graphics::Render_pass;
using erhe::graphics::Texture;

Framebuffer_window::Framebuffer_window(
    erhe::graphics::Device& graphics_device,
    Imgui_renderer&         imgui_renderer,
    Imgui_windows&          imgui_windows,
    const std::string_view  title,
    const char*             ini_label
)
    : Imgui_window     {imgui_renderer, imgui_windows, title, ini_label}
    , m_graphics_device{graphics_device}
    , m_debug_label    {ini_label}
    , m_vertex_input   {graphics_device, erhe::graphics::Vertex_input_state_data{}}
{
}

Framebuffer_window::~Framebuffer_window()
{
}

auto Framebuffer_window::get_size(glm::vec2 available_size) const -> glm::vec2
{
    static_cast<void>(available_size);

    return glm::vec2{256.0f, 256.0f};
}

auto Framebuffer_window::to_content(const glm::vec2 position_in_root) const -> glm::vec2
{
    const float content_x = static_cast<float>(position_in_root.x) - m_content_rect_x;
    const float content_y = static_cast<float>(position_in_root.y) - m_content_rect_y;
    //const float content_flip_y = m_content_rect_height - content_y;
    return glm::vec2{content_x, content_y};
}

auto Framebuffer_window::make_render_command_encoder() -> std::unique_ptr<erhe::graphics::Render_command_encoder>
{
    return m_graphics_device.make_render_command_encoder(*m_render_pass.get());
    //gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_render_pass->gl_name());
    //gl::viewport(m_viewport.x, m_viewport.y, m_viewport.width, m_viewport.height);
}

void Framebuffer_window::update_render_pass()
{
    ERHE_PROFILE_FUNCTION();

    ImGuiWindow* window = GImGui->CurrentWindow;
    const ImVec2 win_min = window->ContentRegionRect.Min;
    const ImVec2 win_max = window->ContentRegionRect.Max;
    const ImVec2 win_size{win_max.x - win_min.x, win_max.y - win_min.y};

    const auto imgui_available_size = win_size;

    if ((imgui_available_size.x < 1) || (imgui_available_size.y < 1)) {
        return;
    }

    const glm::vec2 available_size{
        static_cast<float>(imgui_available_size.x),
        static_cast<float>(imgui_available_size.y),
    };

    const glm::vec2 source_size = get_size(available_size);
    if ((source_size.x == 0) || (source_size.y == 0)) {
        return;
    }

    const float ratio_x   = available_size.x / source_size.x;
    const float ratio_y   = available_size.y / source_size.y;
    const float ratio_min = (std::min)(ratio_x, ratio_y);

    const glm::vec2  texture_size = source_size * ratio_min;
    const glm::ivec2 size{texture_size};

    if (m_texture && (m_texture->get_width() == size.x) && (m_texture->get_height() == size.y)) {
        return;
    }

    m_viewport.width  = size.x;
    m_viewport.height = size.y;

    m_texture = std::make_shared<Texture>(
        m_graphics_device,
        Texture::Create_info{
            .device       = m_graphics_device,
            .target       = gl::Texture_target::texture_2d,
            .pixelformat  = erhe::dataformat::Format::format_8_vec4_srgb,
            .sample_count = 0,
            .width        = m_viewport.width,
            .height       = m_viewport.height,
            .debug_label  = "Framebuffer_window"
        }
    );
    m_texture->set_debug_label(m_debug_label);
    const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
    if (gl::is_command_supported(gl::Command::Command_glClearTexImage)) {
        gl::clear_tex_image(m_texture->gl_name(), 0, gl::Pixel_format::rgba, gl::Pixel_type::float_, &clear_value[0]);
    } else {
        // TODO
    }

    erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
    render_pass_descriptor.color_attachments[0].texture = m_texture.get();
    render_pass_descriptor.debug_label                  = m_debug_label;
    m_render_pass = std::make_unique<Render_pass>(m_graphics_device, render_pass_descriptor);
}

void Framebuffer_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    if (m_texture && (m_texture->get_width() > 0) && (m_texture->get_height() > 0)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        draw_image(m_texture, m_viewport.width, m_viewport.height);
        set_is_window_hovered(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem));
        const ImVec2 rect_min = ImGui::GetItemRectMin();
        const ImVec2 rect_max = ImGui::GetItemRectMax();
        m_content_rect_x      = rect_min.x;
        m_content_rect_y      = rect_min.y;
        m_content_rect_width  = rect_max.x - rect_min.x;
        m_content_rect_height = rect_max.y - rect_min.y;
        ImGui::PopStyleVar();
    }
}

} // namespace erhe::imgui
