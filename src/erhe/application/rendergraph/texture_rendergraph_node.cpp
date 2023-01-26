#include "erhe/application/rendergraph/texture_rendergraph_node.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/gl_helpers.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/gpu_timer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <algorithm>

namespace erhe::application
{

Texture_rendergraph_node::Texture_rendergraph_node(
    const Texture_rendergraph_node_create_info& create_info
)
    : Rendergraph_node      {create_info.name}
    , m_input_key           {create_info.input_key}
    , m_output_key          {create_info.output_key}
    , m_color_format        {create_info.color_format}
    , m_depth_stencil_format{create_info.depth_stencil_format}
{
}

Texture_rendergraph_node::Texture_rendergraph_node(
    const Texture_rendergraph_node_create_info&& create_info
)
    : Rendergraph_node      {create_info.name}
    , m_input_key           {create_info.input_key}
    , m_output_key          {create_info.output_key}
    , m_color_format        {create_info.color_format}
    , m_depth_stencil_format{create_info.depth_stencil_format}
{
}

[[nodiscard]] auto Texture_rendergraph_node::get_consumer_input_texture(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::shared_ptr<erhe::graphics::Texture>
{
    ERHE_VERIFY(depth < rendergraph_max_depth);
    if (key != m_input_key)
    {
        log_rendergraph->error(
            "Texture_rendergraph_node::get_consumer_input_texture({}, key = '{}', depth = {}) key mismatch (expected '{}')",
            c_str(resource_routing),
            key,
            depth,
            m_input_key
        );
        return std::shared_ptr<erhe::graphics::Texture>{};
    }

    if (m_enabled)
    {
        return m_color_texture;
    }

    return get_producer_output_texture(resource_routing, m_output_key, depth + 1);
}

[[nodiscard]] auto Texture_rendergraph_node::get_consumer_input_framebuffer(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::shared_ptr<erhe::graphics::Framebuffer>
{
    ERHE_VERIFY(depth < rendergraph_max_depth);
    if (key != m_input_key)
    {
        log_rendergraph->error(
            "Texture_rendergraph_node::get_consumer_input_texture({}, key = '{}', depth = {}) key mismatch (expected '{}')",
            c_str(resource_routing),
            key,
            depth,
            m_input_key
        );
        return std::shared_ptr<erhe::graphics::Framebuffer>{};
    }

    if (m_enabled)
    {
        return m_framebuffer;
    }

    return get_producer_output_framebuffer(resource_routing, m_output_key, depth + 1);
}

[[nodiscard]] auto Texture_rendergraph_node::get_producer_output_texture(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::shared_ptr<erhe::graphics::Texture>
{
    static_cast<void>(resource_routing);
    static_cast<void>(key);
    static_cast<void>(depth);
    return m_color_texture;
}

[[nodiscard]] auto Texture_rendergraph_node::get_producer_output_framebuffer(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::shared_ptr<erhe::graphics::Framebuffer>
{
    static_cast<void>(resource_routing);
    static_cast<void>(key);
    static_cast<void>(depth);
    return m_framebuffer;
}

void Texture_rendergraph_node::execute_rendergraph_node()
{
    using erhe::graphics::Framebuffer;
    using erhe::graphics::Texture;

    // TODO Figure out exactly what to do here.
    const auto& output_viewport = get_producer_output_viewport(
        Resource_routing::Resource_provided_by_consumer,
        m_output_key
    );

    if (
        (output_viewport.width  < 1) ||
        (output_viewport.height < 1)
    )
    {
        return;
    }

    // Resize framebuffer if necessary
    if (
        !m_color_texture ||
        (m_color_texture->width () != output_viewport.width ) ||
        (m_color_texture->height() != output_viewport.height)
    )
    {
        log_rendergraph->info(
            "Resizing Texture_rendergraph_node '{}' to {} x {}",
            get_name(),
            output_viewport.width,
            output_viewport.height
        );

        //// TODO
        ////    .internal_format = m_configuration->graphics.low_hdr
        ////        ? gl::Internal_format::r11f_g11f_b10f
        ////        : gl::Internal_format::rgba16f,

        m_color_texture = std::make_shared<Texture>(
            Texture::Create_info{
                .target          = gl::Texture_target::texture_2d,
                .internal_format = m_color_format,
                .width           = output_viewport.width,
                .height          = output_viewport.height
            }
        );
        m_color_texture->set_debug_label(
            fmt::format("{} Texture_rendergraph_node color texture", get_name())
        );
        const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
        gl::clear_tex_image(
            m_color_texture->gl_name(),
            0,
            gl::Pixel_format::rgba,
            gl::Pixel_type::float_,
            &clear_value[0]
        );

        if (m_depth_stencil_format == gl::Internal_format{0})
        {
            m_depth_stencil_renderbuffer.reset();
        }
        else
        {
            m_depth_stencil_renderbuffer = std::make_unique<erhe::graphics::Renderbuffer>(
                m_depth_stencil_format,
                output_viewport.width,
                output_viewport.height
            );

            m_depth_stencil_renderbuffer->set_debug_label(
                fmt::format("{} Texture_rendergraph_node depth-stencil renderbuffer", get_name())
            );
        }

        {
            Framebuffer::Create_info create_info;
            create_info.attach(
                gl::Framebuffer_attachment::color_attachment0,
                m_color_texture.get()
            );

            if (m_depth_stencil_renderbuffer)
            {
                if (gl_helpers::has_depth(m_depth_stencil_format))
                {
                    create_info.attach(
                        gl::Framebuffer_attachment::depth_attachment,
                        m_depth_stencil_renderbuffer.get()
                    );
                }
                if (gl_helpers::has_stencil(m_depth_stencil_format))
                {
                    create_info.attach(
                        gl::Framebuffer_attachment::stencil_attachment,
                        m_depth_stencil_renderbuffer.get()
                    );
                }
            }
            m_framebuffer = std::make_unique<Framebuffer>(create_info);
            m_framebuffer->set_debug_label(
                fmt::format("{} Texture_rendergraph_node framebuffer", get_name())
            );

            gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0 };
            gl::named_framebuffer_draw_buffers(
                m_framebuffer->gl_name(),
                1,
                &draw_buffers[0]
            );
            gl::named_framebuffer_read_buffer(
                m_framebuffer->gl_name(),
                gl::Color_buffer::color_attachment0
            );

            if (!m_framebuffer->check_status())
            {
                log_rendergraph->error("{} Texture_rendergraph_node framebuffer not complete", get_name());
                m_framebuffer.reset();
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

} // namespace editor
