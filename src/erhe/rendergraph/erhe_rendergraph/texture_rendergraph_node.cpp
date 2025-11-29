#include "erhe_rendergraph/texture_rendergraph_node.hpp"

#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"

#include <algorithm>

namespace erhe::rendergraph {

Texture_rendergraph_node::Texture_rendergraph_node(const Texture_rendergraph_node_create_info& create_info)
    : Rendergraph_node      {create_info.rendergraph, create_info.name}
    , m_input_key           {create_info.input_key}
    , m_output_key          {create_info.output_key}
    , m_color_format        {create_info.color_format}
    , m_depth_stencil_format{create_info.depth_stencil_format}
    , m_sample_count        {create_info.sample_count}
{
    register_output(create_info.name, erhe::rendergraph::Rendergraph_node_key::viewport_texture);
}

Texture_rendergraph_node::Texture_rendergraph_node(const Texture_rendergraph_node_create_info&& create_info)
    : Rendergraph_node      {create_info.rendergraph, create_info.name}
    , m_input_key           {create_info.input_key}
    , m_output_key          {create_info.output_key}
    , m_color_format        {create_info.color_format}
    , m_depth_stencil_format{create_info.depth_stencil_format}
    , m_sample_count        {create_info.sample_count}
{
    register_output(create_info.name, erhe::rendergraph::Rendergraph_node_key::viewport_texture);
}

Texture_rendergraph_node::~Texture_rendergraph_node() noexcept = default;

auto Texture_rendergraph_node::get_producer_output_texture(int, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_color_texture;
}

void Texture_rendergraph_node::reconfigure(int sample_count)
{
    m_sample_count = sample_count;
    m_color_texture.reset();
    m_render_pass.reset();
}

void Texture_rendergraph_node::update_render_pass(int width, int height, erhe::graphics::Swapchain* swapchain)
{
    erhe::graphics::Device& graphics_device = m_rendergraph.get_graphics_device();

    using erhe::graphics::Render_pass;

    if (swapchain != nullptr) {
        erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
        render_pass_descriptor.swapchain            = swapchain;
        render_pass_descriptor.render_target_width  = width;
        render_pass_descriptor.render_target_height = height;
        render_pass_descriptor.debug_label          = fmt::format("{} Texture_rendergraph_node renderpass (default framebuffer)", get_name());
        m_render_pass = std::make_unique<Render_pass>(graphics_device, render_pass_descriptor);
    }

    using erhe::graphics::Texture;

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
            "Resizing Texture_rendergraph_node '{}' to {} x {}",
            get_name(),
            width,
            height
        );

        //// TODO Consider m_configuration->graphics.low_hdr

        m_multisampled_color_texture.reset();
        m_color_texture.reset();
        if (m_sample_count > 0) {
            m_multisampled_color_texture = std::make_shared<Texture>(
                graphics_device,
                Texture::Create_info{
                    .device       = graphics_device,
                    .type         = erhe::graphics::Texture_type::texture_2d,
                    .pixelformat  = m_color_format,
                    .sample_count = m_sample_count,
                    .width        = width,
                    .height       = height,
                    .debug_label  = fmt::format("{} Texture_rendergraph_node multisampled color texture", get_name())
                }
            );
        }

        m_color_texture = std::make_shared<Texture>(
            graphics_device,
            Texture::Create_info{
                .device       = graphics_device,
                .type         = erhe::graphics::Texture_type::texture_2d,
                .pixelformat  = m_color_format,
                .sample_count = 0,
                .width        = width,
                .height       = height,
                .debug_label  = fmt::format("{} Texture_rendergraph_node color texture", get_name())
            }
        );
        const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
        graphics_device.clear_texture(*m_color_texture.get(), { 1.0, 0.0, 1.0, 1.0 });

        if (m_depth_stencil_format == erhe::dataformat::Format::format_undefined) {
            m_depth_stencil_texture.reset();
        } else {
            m_depth_stencil_texture = std::make_unique<erhe::graphics::Texture>(
                graphics_device,
                Texture::Create_info{
                    .device       = graphics_device,
                    .type         = erhe::graphics::Texture_type::texture_2d,
                    .pixelformat  = m_depth_stencil_format,
                    .sample_count = m_sample_count,
                    .width        = width,
                    .height       = height,
                    .debug_label  = fmt::format("{} Texture_rendergraph_node depth-stencil texture", get_name())
                }
            );
        }

        {
            erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
            if (m_sample_count > 0) {
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
                if (erhe::dataformat::get_depth_size(m_depth_stencil_format) > 0) {
                    render_pass_descriptor.depth_attachment.texture      = m_depth_stencil_texture.get();
                    render_pass_descriptor.depth_attachment.load_action  = erhe::graphics::Load_action::Clear;
                    render_pass_descriptor.depth_attachment.store_action = erhe::graphics::Store_action::Dont_care;
                }
                if (erhe::dataformat::get_stencil_size(m_depth_stencil_format) > 0) {
                    render_pass_descriptor.stencil_attachment.texture      = m_depth_stencil_texture.get();
                    render_pass_descriptor.stencil_attachment.load_action  = erhe::graphics::Load_action::Clear;
                    render_pass_descriptor.stencil_attachment.store_action = erhe::graphics::Store_action::Dont_care;
                }
            }
            render_pass_descriptor.render_target_width  = width;
            render_pass_descriptor.render_target_height = height;
            render_pass_descriptor.debug_label          = fmt::format("{} Texture_rendergraph_node renderpass", get_name());
            m_render_pass = std::make_unique<Render_pass>(graphics_device, render_pass_descriptor);
        }

        // TODO initial clear?
        //const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
        //gl::clear_tex_image(
        //    texture->gl_name(),
        //    0,
        //    gl::Pixel_format::rgba,
        //    gl::Pixel_type::float_,
        //    &clear_value[0]
        //);
    }
}

} // namespace erhe::rendergraph
