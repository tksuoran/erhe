#include "graphics/thumbnails.hpp"
#include "editor_settings.hpp"

#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/renderbuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

Thumbnails::Thumbnails(erhe::graphics::Instance& graphics_instance, const unsigned int capacity, const unsigned int size_pixels)
    : m_color_sampler{
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::linear_mipmap_nearest,
            .mag_filter  = gl::Texture_mag_filter::nearest,
            .debug_label = "Thumbnail sampler"
        }
    }
{
    m_capacity_root = static_cast<unsigned int>(std::sqrt(capacity) + 0.5);
    m_capacity = m_capacity_root * m_capacity_root;
    m_in_use.resize(m_capacity);
    std::fill(m_in_use.begin(), m_in_use.end(), false);
    m_size_pixels = size_pixels;
    const int viewport_size = static_cast<int>(m_capacity_root * m_size_pixels);

    m_color_texture = std::make_shared<erhe::graphics::Texture>(
        erhe::graphics::Texture_create_info{
            .instance        = graphics_instance,
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::rgba8,
            .use_mipmaps     = true,
            .width           = viewport_size,
            .height          = viewport_size,
            .debug_label     = "Thumbnails"
        }
    );

    m_depth_renderbuffer = std::make_unique<erhe::graphics::Renderbuffer>(
        graphics_instance,
        gl::Internal_format::depth_component32f,
        viewport_size,
        viewport_size
    );
    m_depth_renderbuffer->set_debug_label("Thumbnail depth renderbuffer");

    erhe::graphics::Framebuffer::Create_info framebuffer_create_info;
    framebuffer_create_info.attach(gl::Framebuffer_attachment::color_attachment0, m_color_texture.get());
    framebuffer_create_info.attach(gl::Framebuffer_attachment::depth_attachment,  m_depth_renderbuffer.get());
    m_framebuffer = std::make_unique<erhe::graphics::Framebuffer>(framebuffer_create_info);
    m_framebuffer->set_debug_label("Thumbnail framebuffer");

    m_color_texture_handle = graphics_instance.get_handle(*m_color_texture.get(), m_color_sampler);
}

auto Thumbnails::allocate() -> uint32_t
{
    for (size_t i = 0, end = m_in_use.size(); i < end; ++i) {
        if (!m_in_use[i]) {
            m_in_use[i] = true;
            return static_cast<uint32_t>(i);
        }
    }
    ERHE_FATAL("out of thumbnail slots");
    return std::numeric_limits<uint32_t>::max();
}

void Thumbnails::free(uint32_t slot)
{
    ERHE_VERIFY(m_in_use[slot]);
    m_in_use[slot] = true;
}

void Thumbnails::begin_capture(uint32_t slot) const
{
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());

#if !defined(NDEBUG)
    const auto status = gl::check_named_framebuffer_status(m_framebuffer->gl_name(), gl::Framebuffer_target::draw_framebuffer);
    ERHE_VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
#endif

    const uint32_t slot_x = slot % m_capacity_root;
    const uint32_t slot_y = slot / m_capacity_root;
    const uint32_t x0     = slot_x * m_size_pixels;
    const uint32_t y0     = slot_y * m_size_pixels;
    gl::viewport   (x0, y0, m_size_pixels, m_size_pixels);
    gl::clear_color(0.0f, 0.0f, 0.0f, 0.0f);
    gl::clear      (gl::Clear_buffer_mask::color_buffer_bit | gl::Clear_buffer_mask::depth_buffer_bit);
}

void Thumbnails::draw(uint32_t slot) const
{
    static_cast<void>(slot);
    //m_context.imgui_renderer->image(
    //    m_texture,
    //    m_icon_width,
    //    m_icon_height,
    //    uv0,
    //    uv1(uv0),
    //    imvec_from_glm(tint_color),
    //    false
    //);
    //ImGui::SameLine();
}

//auto Icon_rasterization::icon_button(
//    const uint32_t  id,
//    const glm::vec2 uv0,
//    const glm::vec4 background_color,
//    const glm::vec4 tint_color,
//    const bool      linear
//) const -> bool
//{
//#if !defined(ERHE_GUI_LIBRARY_IMGUI)
//    static_cast<void>(uv0);
//    static_cast<void>(tint_color);
//    return false;
//#else
//    ERHE_PROFILE_FUNCTION();
//
//    const bool result = m_context.imgui_renderer->image_button(id, m_texture, m_icon_width, m_icon_height, uv0, uv1(uv0), background_color, tint_color, linear);
//    ImGui::SameLine();
//    return result;
//#endif
//}

} // namespace editor
