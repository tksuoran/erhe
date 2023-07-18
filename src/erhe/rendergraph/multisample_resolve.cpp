#include "erhe/rendergraph/multisample_resolve.hpp"

#include "erhe/rendergraph/rendergraph.hpp"
#include "erhe/rendergraph/rendergraph_log.hpp"
#include "erhe/gl/command_info.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <algorithm>

namespace erhe::rendergraph
{

Multisample_resolve_node::Multisample_resolve_node(
    Rendergraph&           rendergraph,
    const std::string_view name,
    const int              sample_count,
    const std::string_view label,
    const int              key

)
    : Rendergraph_node{rendergraph, name}
    , m_sample_count  {sample_count}
    , m_label         {label}
    , m_key           {key}
{
    register_input (Resource_routing::Resource_provided_by_consumer, label, key);
    register_output(Resource_routing::Resource_provided_by_consumer, label, key);
}

void Multisample_resolve_node::reconfigure(const int sample_count)
{
    if (m_sample_count == sample_count) {
        return;
    }

    m_sample_count = sample_count;
    m_color_texture.reset();
    m_depth_stencil_renderbuffer.reset();
    m_framebuffer.reset();
}

[[nodiscard]] auto Multisample_resolve_node::get_consumer_input_texture(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::shared_ptr<erhe::graphics::Texture>
{
    ERHE_VERIFY(depth < rendergraph_max_depth);

    // TODO Validate resource_routing?
    if (m_enabled) {
        // TODO Validate key
        static_cast<void>(key);
        return m_color_texture;
    }

    // Pass-through by connecting input directly to output
    return get_producer_output_texture(resource_routing, key, depth + 1);
}

[[nodiscard]] auto Multisample_resolve_node::get_consumer_input_framebuffer(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::shared_ptr<erhe::graphics::Framebuffer>
{
    ERHE_VERIFY(depth < rendergraph_max_depth);

    // TODO Validate resource_routing?
    if (m_enabled) {
        // TODO check key
        static_cast<void>(key);
        return m_framebuffer;
    }

    // Pass-through by connecting input directly to output
    return get_producer_output_framebuffer(resource_routing, key, depth + 1);
}

[[nodiscard]] auto Multisample_resolve_node::get_consumer_input_viewport(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> erhe::toolkit::Viewport
{
    return get_producer_output_viewport(resource_routing, key, depth + 1);
}

void Multisample_resolve_node::execute_rendergraph_node()
{
    ERHE_PROFILE_FUNCTION();

    using erhe::graphics::Framebuffer;
    using erhe::graphics::Texture;

    const auto& output_viewport = get_producer_output_viewport(
        Resource_routing::Resource_provided_by_consumer,
        m_key
    );

    if (
        (output_viewport.width  < 1) ||
        (output_viewport.height < 1)
    ) {
        return;
    }

    // Resize framebuffer if necessary
    if (
        !m_color_texture ||
        (m_color_texture->width () != output_viewport.width ) ||
        (m_color_texture->height() != output_viewport.height)
    ) {
        log_tail->trace(
            "Resizing Multisample_resolve_node '{}' to {} x {}",
            get_name(),
            output_viewport.width,
            output_viewport.height
        );

        //// TODO Mirror framebuffer from output rendergraph node:
        ////      Get attachments and their formats from output rendergraph node
        erhe::graphics::Instance& graphics_instance = m_rendergraph.get_graphics_instance();

        m_color_texture = std::make_shared<Texture>(
            Texture::Create_info{
                .instance        = graphics_instance,
                .target          = gl::Texture_target::texture_2d_multisample,
                .internal_format = gl::Internal_format::rgba16f, // TODO other formats
                .sample_count    = m_sample_count,
                .width           = output_viewport.width,
                .height          = output_viewport.height
            }
        );
        m_color_texture->set_debug_label(
            fmt::format("{} Multisample_resolve_node color texture", get_name())
        );
        const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
        if (gl::is_command_supported(gl::Command::Command_glClearTexImage)) {
            gl::clear_tex_image(
                m_color_texture->gl_name(),
                0,
                gl::Pixel_format::rgba,
                gl::Pixel_type::float_,
                &clear_value[0]
            );
        } else {
            // TODO
        }

        m_depth_stencil_renderbuffer = std::make_unique<erhe::graphics::Renderbuffer>(
            graphics_instance,
            gl::Internal_format::depth24_stencil8,
            m_sample_count,
            output_viewport.width,
            output_viewport.height
        );

        m_depth_stencil_renderbuffer->set_debug_label(
            fmt::format("'{}' Multisample_resolve_node depth-stencil renderbuffer", get_name())
        );

        {
            Framebuffer::Create_info create_info;
            create_info.attach(gl::Framebuffer_attachment::color_attachment0,  m_color_texture.get());
            create_info.attach(gl::Framebuffer_attachment::depth_attachment,   m_depth_stencil_renderbuffer.get());
            create_info.attach(gl::Framebuffer_attachment::stencil_attachment, m_depth_stencil_renderbuffer.get());
            m_framebuffer = std::make_unique<Framebuffer>(create_info);
            m_framebuffer->set_debug_label(
                fmt::format("{} Multisample_resolve_node framebuffer", get_name())
            );

            gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0 };
            gl::named_framebuffer_draw_buffers(m_framebuffer->gl_name(), 1, &draw_buffers[0]);
            gl::named_framebuffer_read_buffer (m_framebuffer->gl_name(), gl::Color_buffer::color_attachment0);

            if (!m_framebuffer->check_status()) {
                log_frame->error("'{} Multisample_resolve_node framebuffer not complete", get_name());
                m_framebuffer.reset();
                return;
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

        erhe::graphics::Framebuffer::Create_info create_info;
        create_info.attach(gl::Framebuffer_attachment::color_attachment0,  m_color_texture.get());
        create_info.attach(gl::Framebuffer_attachment::depth_attachment,   m_depth_stencil_renderbuffer.get());
        create_info.attach(gl::Framebuffer_attachment::stencil_attachment, m_depth_stencil_renderbuffer.get());
        m_framebuffer = std::make_shared<Framebuffer>(create_info);
        m_framebuffer->set_debug_label(get_name());

        gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0 };
        gl::named_framebuffer_draw_buffers(m_framebuffer->gl_name(), 1, &draw_buffers[0]);
        gl::named_framebuffer_read_buffer (m_framebuffer->gl_name(), gl::Color_buffer::color_attachment0);

        if (!m_framebuffer->check_status()) {
            log_frame->error("{} Multisample_resolve_node framebuffer not complete", get_name());
            m_framebuffer.reset();
            return;
        }
    }

    {
        ERHE_PROFILE_SCOPE("blitframebuffer");

        const auto& output_framebuffer = get_producer_output_framebuffer(
            Resource_routing::Resource_provided_by_consumer,
            Rendergraph_node_key::viewport
        );
        const GLint output_framebuffer_name = output_framebuffer
            ? output_framebuffer->gl_name()
            : 0;

        gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, m_framebuffer->gl_name());
#if !defined(NDEBUG)
        {
            const auto status = gl::check_named_framebuffer_status(
                m_framebuffer->gl_name(),
                gl::Framebuffer_target::draw_framebuffer
            );
            if (status != gl::Framebuffer_status::framebuffer_complete) {
                log_frame->error(
                    "{} Multisample_resolve_node BlitFramebuffer read framebuffer status = {}",
                    get_name(),
                    gl::c_str(status)
                );
            }
            ERHE_VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
        }
#endif

        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, output_framebuffer_name);
#if !defined(NDEBUG)
        if (output_framebuffer_name != 0) {
            const auto status = gl::check_named_framebuffer_status(
                output_framebuffer_name,
                gl::Framebuffer_target::draw_framebuffer
            );
            if (status != gl::Framebuffer_status::framebuffer_complete) {
                log_frame->error(
                    "{} Multisample_resolve_node BlitFramebuffer draw framebuffer status = {}",
                    get_name(),
                    gl::c_str(status)
                );
            }
            ERHE_VERIFY(status == gl::Framebuffer_status::framebuffer_complete);
        }
#endif

        // TODO scissor test *should* be disabled by default
        gl::blit_framebuffer(
            0,                                          // src X0
            0,                                          // src Y1
            output_viewport.width,                      // src X0
            output_viewport.height,                     // src Y1
            output_viewport.x,                          // dst X0
            output_viewport.y,                          // dst Y1
            output_viewport.x + output_viewport.width,  // dst X0
            output_viewport.y + output_viewport.height, // dst Y1
            gl::Clear_buffer_mask::color_buffer_bit,    // mask
            gl::Blit_framebuffer_filter::nearest        // filter
        );

        // TODO remove unbind
        gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, 0);
        gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    }
}

} // namespace erhe::rendergraph
