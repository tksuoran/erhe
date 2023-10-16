#include "erhe_imgui/windows/framebuffer_window.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_graphics/gl_context_provider.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>


namespace erhe::imgui
{

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

Framebuffer_window::Framebuffer_window(
    erhe::graphics::Instance& graphics_instance,
    Imgui_renderer&           imgui_renderer,
    Imgui_windows&            imgui_windows,
    const std::string_view    title,
    const char*               ini_label
)
    : Imgui_window              {imgui_renderer, imgui_windows, title, ini_label}
    , m_graphics_instance       {graphics_instance}
    , m_debug_label             {ini_label}
    , m_empty_attribute_mappings{graphics_instance, {}}
    , m_vertex_input{
        erhe::graphics::Vertex_input_state_data{}
    }
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

void Framebuffer_window::bind_framebuffer()
{
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
    gl::viewport        (m_viewport.x, m_viewport.y, m_viewport.width, m_viewport.height);
}

void Framebuffer_window::update_framebuffer()
{
    ERHE_PROFILE_FUNCTION();

    const auto win_min = ImGui::GetWindowContentRegionMin();
    const auto win_max = ImGui::GetWindowContentRegionMax();

    const ImVec2 win_size{
        win_max.x - win_min.x,
        win_max.y - win_min.y
    };

    const auto imgui_available_size = win_size;

    if (
        (imgui_available_size.x < 1) ||
        (imgui_available_size.y < 1)
    ) {
        return;
    }

    const glm::vec2 available_size{
        static_cast<float>(imgui_available_size.x),
        static_cast<float>(imgui_available_size.y),
    };

    const glm::vec2 source_size = get_size(available_size);
    if (
        (source_size.x == 0) ||
        (source_size.y == 0)
    ) {
        return;
    }

    const float ratio_x   = available_size.x / source_size.x;
    const float ratio_y   = available_size.y / source_size.y;
    const float ratio_min = (std::min)(ratio_x, ratio_y);

    const glm::vec2  texture_size = source_size * ratio_min;
    const glm::ivec2 size{texture_size};

    if (
        m_texture &&
        (m_texture->width()  == size.x) &&
        (m_texture->height() == size.y)
    ) {
        return;
    }

    m_viewport.width  = size.x;
    m_viewport.height = size.y;

    m_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .instance        = m_graphics_instance,
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::srgb8_alpha8,
            .sample_count    = 0,
            .width           = m_viewport.width,
            .height          = m_viewport.height
        }
    );
    m_texture->set_debug_label(m_debug_label);
    const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
    if (gl::is_command_supported(gl::Command::Command_glClearTexImage)) {
        gl::clear_tex_image(
            m_texture->gl_name(),
            0,
            gl::Pixel_format::rgba,
            gl::Pixel_type::float_,
            &clear_value[0]
        );
    } else {
        // TODO
    }

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::color_attachment0, m_texture.get());
    m_framebuffer = std::make_unique<Framebuffer>(create_info);
    m_framebuffer->set_debug_label(m_debug_label);
}

void Framebuffer_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    if (
        m_texture &&
        (m_texture->width() > 0) &&
        (m_texture->height() > 0)
    ) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        image(
            m_texture,
            m_viewport.width,
            m_viewport.height
        );
        m_is_hovered = ImGui::IsItemHovered();
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
