#include "graphics/thumbnails.hpp"
#include "app_context.hpp"
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

void Thumbnails::draw(std::shared_ptr<erhe::Item_base> item, std::function<void()> callback)
{
    const std::size_t item_id = item->get_id();
    uint64_t oldest_frame_number = m_thumbnails[0].last_use_frame_number;
    Thumbnail* oldest_thumbnail = &m_thumbnails[0];
    for (size_t i = 0, end = m_thumbnails.size(); i < end; ++i) {
        Thumbnail& thumbnail = m_thumbnails[i];
        if (
            (thumbnail.item_id == item_id) &&
            !thumbnail.callback.has_value()
        ) {
            thumbnail.last_use_frame_number = m_context.graphics_device->get_frame_number();
            m_context.imgui_renderer->image(
                thumbnail.texture_view,
                m_size_pixels,
                m_size_pixels,
                glm::vec2{0.0f, 0.0f},
                glm::vec2{1.0f, 1.0f},
                glm::vec4{0.0f, 0.0f, 0.0f, 1.0f},
                glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                false
            );
            ImGui::SameLine();
            return;
        }
        if (thumbnail.last_use_frame_number < oldest_frame_number) {
            oldest_frame_number = thumbnail.last_use_frame_number;
            oldest_thumbnail = &thumbnail;
        }
    }

    oldest_thumbnail->callback = callback;
    ImGui::Dummy(ImVec2{static_cast<float>(m_size_pixels), static_cast<float>(m_size_pixels)});

    ImGui::SameLine();
}

void Thumbnails::update()
{
    for (size_t i = 0, end = m_thumbnails.size(); i < end; ++i) {
        Thumbnail& thumbnail = m_thumbnails[i];
        if (thumbnail.callback) {
            erhe::graphics::Render_command_encoder encoder = m_context.graphics_device->make_render_command_encoder(*thumbnail.render_pass.get());
            thumbnail.callback.value()();
            thumbnail.last_use_frame_number = m_context.graphics_device->get_frame_number();
            thumbnail.callback.reset();
        }
    }
}

}
