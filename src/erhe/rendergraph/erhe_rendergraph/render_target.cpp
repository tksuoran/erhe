#include "erhe_rendergraph/render_target.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"

#include <fmt/format.h>

namespace erhe::rendergraph {

Render_target::Render_target(const Render_target_create_info& create_info)
    : m_graphics_device    {create_info.graphics_device}
    , m_debug_label        {create_info.debug_label}
    , m_color_format       {create_info.color_format}
    , m_depth_stencil_format{create_info.depth_stencil_format}
    , m_sample_count       {create_info.sample_count}
{
}

Render_target::~Render_target() noexcept = default;

auto Render_target::get_color_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_color_texture;
}

auto Render_target::get_depth_stencil_texture() const -> erhe::graphics::Texture*
{
    return m_depth_stencil_texture.get();
}

auto Render_target::get_render_pass() const -> erhe::graphics::Render_pass*
{
    return m_render_pass.get();
}

void Render_target::reconfigure(int sample_count)
{
    m_sample_count = sample_count;
    m_color_texture.reset();
    m_render_pass.reset();
}

void Render_target::set_reverse_depth(const bool reverse_depth)
{
    if (m_reverse_depth == reverse_depth) {
        return;
    }
    m_reverse_depth = reverse_depth;
    // Force render pass recreation to update depth clear value
    m_color_texture.reset();
    m_render_pass.reset();
}

void Render_target::update(int width, int height, erhe::graphics::Swapchain* swapchain)
{
    using erhe::graphics::Render_pass;
    using erhe::graphics::Texture;

    if (swapchain != nullptr) {
        erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
        render_pass_descriptor.swapchain            = swapchain;
        render_pass_descriptor.render_target_width  = width;
        render_pass_descriptor.render_target_height = height;
        render_pass_descriptor.debug_label          = m_debug_label;
        m_render_pass = std::make_unique<Render_pass>(m_graphics_device, render_pass_descriptor);
    }

    if ((width < 1) || (height < 1)) {
        if (m_render_pass) {
            m_color_texture.reset();
            m_multisampled_color_texture.reset();
            m_depth_stencil_texture.reset();
            m_render_pass.reset();
        }
        return;
    }

    // Resize framebuffer if necessary
    if (
        !m_color_texture ||
        (m_color_texture->get_width () != width ) ||
        (m_color_texture->get_height() != height)
    ) {
        log_tail->trace(
            "Resizing Render_target '{}' to {} x {}",
            m_debug_label.string_view(),
            width,
            height
        );

        m_multisampled_color_texture.reset();
        m_color_texture.reset();
        if (m_sample_count > 1) {
            m_multisampled_color_texture = std::make_shared<Texture>(
                m_graphics_device,
                erhe::graphics::Texture_create_info{
                    .device       = m_graphics_device,
                    .usage_mask   =
                        erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                        erhe::graphics::Image_usage_flag_bit_mask::sampled          |
                        erhe::graphics::Image_usage_flag_bit_mask::transfer_src     |
                        erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
                    .type         = erhe::graphics::Texture_type::texture_2d,
                    .pixelformat  = m_color_format,
                    .sample_count = m_sample_count,
                    .width        = width,
                    .height       = height,
                    .debug_label  = erhe::utility::Debug_label{
                        fmt::format("{} multisampled color texture", m_debug_label.string_view())
                    }
                }
            );
        }

        m_color_texture = std::make_shared<Texture>(
            m_graphics_device,
            erhe::graphics::Texture_create_info{
                .device       = m_graphics_device,
                .usage_mask   =
                    erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                    erhe::graphics::Image_usage_flag_bit_mask::sampled          |
                    erhe::graphics::Image_usage_flag_bit_mask::transfer_src     |
                    erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
                .type         = erhe::graphics::Texture_type::texture_2d,
                .pixelformat  = m_color_format,
                .sample_count = 0,
                .width        = width,
                .height       = height,
                .debug_label  = erhe::utility::Debug_label{
                    fmt::format("{} color texture", m_debug_label.string_view())
                }
            }
        );
        m_graphics_device.clear_texture(*m_color_texture.get(), { 1.0, 0.0, 1.0, 1.0 });

        if (m_depth_stencil_format == erhe::dataformat::Format::format_undefined) {
            m_depth_stencil_texture.reset();
        } else {
            m_depth_stencil_texture = std::make_unique<erhe::graphics::Texture>(
                m_graphics_device,
                erhe::graphics::Texture_create_info{
                    .device       = m_graphics_device,
                    .usage_mask   =
                        erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
                        erhe::graphics::Image_usage_flag_bit_mask::sampled,
                    .type         = erhe::graphics::Texture_type::texture_2d,
                    .pixelformat  = m_depth_stencil_format,
                    .sample_count = m_sample_count,
                    .width        = width,
                    .height       = height,
                    .debug_label  = erhe::utility::Debug_label{
                        fmt::format("{} depth-stencil texture", m_debug_label.string_view())
                    }
                }
            );
        }

        {
            erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
            if (m_sample_count > 1) {
                render_pass_descriptor.color_attachments[0].texture         = m_multisampled_color_texture.get();
                render_pass_descriptor.color_attachments[0].resolve_texture = m_color_texture.get();
                render_pass_descriptor.color_attachments[0].load_action     = erhe::graphics::Load_action::Clear;
                render_pass_descriptor.color_attachments[0].store_action    = erhe::graphics::Store_action::Multisample_resolve;
            } else {
                render_pass_descriptor.color_attachments[0].texture         = m_color_texture.get();
                render_pass_descriptor.color_attachments[0].load_action     = erhe::graphics::Load_action::Clear;
                render_pass_descriptor.color_attachments[0].store_action    = erhe::graphics::Store_action::Store;
            }
            if (m_depth_stencil_texture) {
                if (erhe::dataformat::get_depth_size_bits(m_depth_stencil_format) > 0) {
                    render_pass_descriptor.depth_attachment.texture         = m_depth_stencil_texture.get();
                    render_pass_descriptor.depth_attachment.load_action     = erhe::graphics::Load_action::Clear;
                    render_pass_descriptor.depth_attachment.store_action    = erhe::graphics::Store_action::Dont_care;
                    render_pass_descriptor.depth_attachment.clear_value[0]  = m_reverse_depth ? 0.0 : 1.0;
                }
                if (erhe::dataformat::get_stencil_size_bits(m_depth_stencil_format) > 0) {
                    render_pass_descriptor.stencil_attachment.texture      = m_depth_stencil_texture.get();
                    render_pass_descriptor.stencil_attachment.load_action  = erhe::graphics::Load_action::Clear;
                    render_pass_descriptor.stencil_attachment.store_action = erhe::graphics::Store_action::Dont_care;
                }
            }
            render_pass_descriptor.render_target_width  = width;
            render_pass_descriptor.render_target_height = height;
            render_pass_descriptor.debug_label          = erhe::utility::Debug_label{
                fmt::format("{} renderpass", m_debug_label.string_view())
            };
            m_render_pass = std::make_unique<Render_pass>(m_graphics_device, render_pass_descriptor);
        }
    }
}

} // namespace erhe::rendergraph
