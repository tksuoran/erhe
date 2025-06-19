#include "erhe_rendergraph/texture_rendergraph_node.hpp"

#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_gl/command_info.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/renderbuffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::rendergraph {

Texture_rendergraph_node::Texture_rendergraph_node(const Texture_rendergraph_node_create_info& create_info)
    : Rendergraph_node      {create_info.rendergraph, create_info.name}
    , m_input_key           {create_info.input_key}
    , m_output_key          {create_info.output_key}
    , m_color_format        {create_info.color_format}
    , m_depth_stencil_format{create_info.depth_stencil_format}
{
}

Texture_rendergraph_node::Texture_rendergraph_node(const Texture_rendergraph_node_create_info&& create_info)
    : Rendergraph_node      {create_info.rendergraph, create_info.name}
    , m_input_key           {create_info.input_key}
    , m_output_key          {create_info.output_key}
    , m_color_format        {create_info.color_format}
    , m_depth_stencil_format{create_info.depth_stencil_format}
{
}

Texture_rendergraph_node::~Texture_rendergraph_node() noexcept = default;

auto Texture_rendergraph_node::get_consumer_input_texture(const Routing resource_routing, const int key, const int depth) const -> std::shared_ptr<erhe::graphics::Texture>
{
    ERHE_VERIFY(depth < rendergraph_max_depth);
    if (key != m_input_key) {
        log_tail->error(
            "Texture_rendergraph_node::get_consumer_input_texture({}, key = '{}', depth = {}) key mismatch (expected '{}')",
            c_str(resource_routing),
            key,
            depth,
            m_input_key
        );
        return std::shared_ptr<erhe::graphics::Texture>{};
    }

    if (m_enabled) {
        return m_color_texture;
    }

    return get_producer_output_texture(resource_routing, m_output_key, depth + 1);
}

auto Texture_rendergraph_node::get_consumer_input_render_pass(const Routing resource_routing, const int key, const int depth) const -> erhe::graphics::Render_pass*
{
    ERHE_VERIFY(depth < rendergraph_max_depth);
    if (key != m_input_key) {
        log_tail->error(
            "Texture_rendergraph_node::get_consumer_input_texture({}, key = '{}', depth = {}) key mismatch (expected '{}')",
            c_str(resource_routing),
            key,
            depth,
            m_input_key
        );
        return nullptr;
    }

    if (m_enabled) {
        return m_render_pass.get();
    }

    return get_producer_output_render_pass(resource_routing, m_output_key, depth + 1);
}

auto Texture_rendergraph_node::get_producer_output_texture(Routing, int, int) const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_color_texture;
}

auto Texture_rendergraph_node::get_producer_output_render_pass(Routing, int, int) const -> erhe::graphics::Render_pass*
{
    return m_render_pass.get();
}

void Texture_rendergraph_node::execute_rendergraph_node()
{
    using erhe::graphics::Render_pass;
    using erhe::graphics::Texture;

    // TODO Figure out exactly what to do here.
    const auto& output_viewport = get_producer_output_viewport(Routing::Resource_provided_by_consumer, m_output_key);

    if ((output_viewport.width  < 1) || (output_viewport.height < 1)) {
        return;
    }

    // Resize framebuffer if necessary
    if (
        !m_color_texture ||
        (m_color_texture->get_width () != output_viewport.width ) ||
        (m_color_texture->get_height() != output_viewport.height)
    ) {
        log_tail->trace(
            "Resizing Texture_rendergraph_node '{}' to {} x {}",
            get_name(),
            output_viewport.width,
            output_viewport.height
        );

        //// TODO Consider m_configuration->graphics.low_hdr
        erhe::graphics::Device& graphics_device = m_rendergraph.get_graphics_device();

        m_color_texture = std::make_shared<Texture>(
            graphics_device,
            Texture::Create_info{
                .device      = graphics_device,
                .target      = gl::Texture_target::texture_2d,
                .pixelformat = m_color_format,
                .width       = output_viewport.width,
                .height      = output_viewport.height,
                .debug_label = fmt::format("{} Texture_rendergraph_node color texture", get_name())
            }
        );
        const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
        gl::clear_tex_image(m_color_texture->gl_name(), 0, gl::Pixel_format::rgba, gl::Pixel_type::float_, &clear_value[0]);

        if (m_depth_stencil_format == erhe::dataformat::Format::format_undefined) {
            m_depth_stencil_renderbuffer.reset();
        } else {
            m_depth_stencil_renderbuffer = std::make_unique<erhe::graphics::Renderbuffer>(
                graphics_device,
                m_depth_stencil_format,
                output_viewport.width,
                output_viewport.height
            );

            m_depth_stencil_renderbuffer->set_debug_label(
                fmt::format("{} Texture_rendergraph_node depth-stencil renderbuffer", get_name())
            );
        }

        {
            erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
            render_pass_descriptor.color_attachments[0].texture      = m_color_texture.get();
            render_pass_descriptor.color_attachments[0].load_action  = erhe::graphics::Load_action::Clear;
            render_pass_descriptor.color_attachments[0].store_action = erhe::graphics::Store_action::Store;
            render_pass_descriptor.render_target_width               = output_viewport.width;
            render_pass_descriptor.render_target_height              = output_viewport.height;
            render_pass_descriptor.debug_label                       = fmt::format("{} Texture_rendergraph_node framebuffer", get_name());

            if (m_depth_stencil_renderbuffer) {
                if (erhe::dataformat::get_depth_size(m_depth_stencil_format) > 0) {
                    render_pass_descriptor.depth_attachment.renderbuffer = m_depth_stencil_renderbuffer.get();
                    render_pass_descriptor.depth_attachment.load_action  = erhe::graphics::Load_action::Clear;
                    render_pass_descriptor.depth_attachment.store_action = erhe::graphics::Store_action::Dont_care;
                }
                if (erhe::dataformat::get_stencil_size(m_depth_stencil_format) > 0) {
                    render_pass_descriptor.stencil_attachment.renderbuffer = m_depth_stencil_renderbuffer.get();
                    render_pass_descriptor.stencil_attachment.load_action  = erhe::graphics::Load_action::Clear;
                    render_pass_descriptor.stencil_attachment.store_action = erhe::graphics::Store_action::Dont_care;
                }
            }
            m_render_pass = std::make_unique<Render_pass>(graphics_device, render_pass_descriptor);

            if (!m_render_pass->check_status()) {
                log_tail->error("{} Texture_rendergraph_node framebuffer not complete", get_name());
                m_render_pass.reset();
            }
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
