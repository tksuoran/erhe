#include "graphics/thumbnails.hpp"
#include "app_settings.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/renderbuffer.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace editor {

Thumbnail::Thumbnail()
{
}

Thumbnail::Thumbnail(Thumbnail&&) = default;

auto Thumbnail::operator=(Thumbnail&&) -> Thumbnail& = default;

Thumbnail::~Thumbnail() = default;

Thumbnails::Thumbnails(erhe::graphics::Device& graphics_device, App_context& context)
    : m_context{context}
    , m_graphics_device{graphics_device}
    , m_color_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::linear,
            .mag_filter  = gl::Texture_mag_filter::nearest,
            .debug_label = "Thumbnail sampler"
        }
    }
{
    int capacity = 0;
    int size_pixels = 0;
    const auto& section = erhe::configuration::get_ini_file_section("erhe.ini", "thumbnails");
    section.get("capacity", capacity);
    section.get("capacity", size_pixels);

    m_thumbnails.resize(capacity);
    m_size_pixels = static_cast<unsigned int>(size_pixels);
    m_color_texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device            = graphics_device,
            .type              = erhe::graphics::Texture_type::texture_2d,
            .pixelformat       = erhe::dataformat::Format::format_8_vec4_unorm, // TODO sRGB?
            .use_mipmaps       = true,
            .width             = m_size_pixels,
            .height            = m_size_pixels,
            .array_layer_count = capacity,
            .debug_label       = "Thumbnails"
        }
    );

    m_depth_renderbuffer = std::make_unique<erhe::graphics::Renderbuffer>(
        graphics_device,
        erhe::dataformat::Format::format_d32_sfloat,
        m_size_pixels,
        m_size_pixels
    );
    m_depth_renderbuffer->set_debug_label("Thumbnail depth renderbuffer");

    for (int i = 0; i < capacity; ++i) {
        Thumbnail& t = m_thumbnails[i];

        erhe::graphics::Texture_create_info texture_create_info = erhe::graphics::Texture_create_info::make_view(m_graphics_device, m_color_texture);
        texture_create_info.view_base_level       = 0;
        texture_create_info.level_count           = 1;
        texture_create_info.view_base_array_layer = i;
        texture_create_info.debug_label           = fmt::format("Thumbnail layer {}", i);
        t.texture_view = std::make_shared<erhe::graphics::Texture>(m_graphics_device, texture_create_info);

        erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
        render_pass_descriptor.color_attachments[0].texture      = t.texture_view.get();
        render_pass_descriptor.color_attachments[0].load_action  = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.color_attachments[0].store_action = erhe::graphics::Store_action::Store;
        render_pass_descriptor.depth_attachment.renderbuffer     = m_depth_renderbuffer.get();
        render_pass_descriptor.depth_attachment.load_action      = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.depth_attachment.store_action     = erhe::graphics::Store_action::Dont_care;
        render_pass_descriptor.depth_attachment.clear_value[0]   = 0.0f; // Reverse Z clear value is 0.0
        render_pass_descriptor.render_target_width               = m_size_pixels;
        render_pass_descriptor.render_target_height              = m_size_pixels;
        render_pass_descriptor.debug_label                       = fmt::format("Thumbnail render pass layer {}", i);
        t.render_pass = std::make_unique<erhe::graphics::Render_pass>(graphics_device, render_pass_descriptor);
        t.color_texture_handle = graphics_device.get_handle(*t.texture_view.get(), m_color_sampler);
    }
}

Thumbnails::~Thumbnails()
{
}

//auto Thumbnails::allocate() -> uint32_t
//{
//    for (size_t i = 0, end = m_last_use_frame_number.size(); i < end; ++i) {
//        if (!m_in_use[i]) {
//            m_in_use[i] = true;
//            return static_cast<uint32_t>(i);
//        }
//    }
//    ERHE_FATAL("out of thumbnail slots");
//    return std::numeric_limits<uint32_t>::max();
//}
//
//void Thumbnails::free(uint32_t slot)
//{
//    ERHE_VERIFY(m_in_use[slot]);
//    m_in_use[slot] = true;
//}
//
//auto Thumbnails::begin_capture(uint32_t slot) -> std::unique_ptr<erhe::graphics::Render_command_encoder>
//{
//    return m_graphics_device.make_render_command_encoder(*m_render_passes[slot].get());
//}

void Thumbnails::draw(
    std::shared_ptr<erhe::Item_base>            item,
    std::function<bool(Thumbnails& thumbnails)> callback
)
{
    static_cast<void>(item);
    static_cast<void>(callback);
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

}
