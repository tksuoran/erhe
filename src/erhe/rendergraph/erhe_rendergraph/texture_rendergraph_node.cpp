#include "erhe_rendergraph/texture_rendergraph_node.hpp"

#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_bit/bit_helpers.hpp"

#include "igl/Device.h"
#include "igl/Framebuffer.h"
#include "igl/Texture.h"

#include <algorithm>

namespace erhe::rendergraph
{

Texture_rendergraph_node::Texture_rendergraph_node(
    const Texture_rendergraph_node_create_info& create_info
)
    : Rendergraph_node      {create_info.rendergraph, create_info.name}
    , m_input_key           {create_info.input_key}
    , m_output_key          {create_info.output_key}
    , m_label               {create_info.label}
    , m_sample_count        {create_info.sample_count}
    , m_color_format        {create_info.color_format}
    , m_depth_stencil_format{create_info.depth_stencil_format}
{
}

Texture_rendergraph_node::Texture_rendergraph_node(
    const Texture_rendergraph_node_create_info&& create_info
)
    : Rendergraph_node      {create_info.rendergraph, create_info.name}
    , m_input_key           {create_info.input_key}
    , m_output_key          {create_info.output_key}
    , m_label               {create_info.label}
    , m_sample_count        {create_info.sample_count}
    , m_color_format        {create_info.color_format}
    , m_depth_stencil_format{create_info.depth_stencil_format}
{
}

Texture_rendergraph_node::~Texture_rendergraph_node() noexcept = default;

[[nodiscard]] auto Texture_rendergraph_node::get_consumer_input_texture(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::shared_ptr<igl::ITexture>
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
        return std::shared_ptr<igl::ITexture>{};
    }

    if (m_enabled) {
        return m_color_texture;
    }

    return get_producer_output_texture(resource_routing, m_output_key, depth + 1);
}

[[nodiscard]] auto Texture_rendergraph_node::get_consumer_input_framebuffer(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::shared_ptr<igl::IFramebuffer>
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
        return std::shared_ptr<igl::IFramebuffer>{};
    }

    if (m_enabled) {
        return m_framebuffer;
    }

    return get_producer_output_framebuffer(resource_routing, m_output_key, depth + 1);
}

[[nodiscard]] auto Texture_rendergraph_node::get_producer_output_texture(
    const Resource_routing resource_routing,
    const int              key,
    const int              depth
) const -> std::shared_ptr<igl::ITexture>
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
) const -> std::shared_ptr<igl::IFramebuffer>
{
    static_cast<void>(resource_routing);
    static_cast<void>(key);
    static_cast<void>(depth);
    return m_framebuffer;
}

void Texture_rendergraph_node::execute_rendergraph_node()
{
    using igl::IFramebuffer;
    using igl::ITexture;

    // TODO Figure out exactly what to do here.
    const auto& output_viewport = get_producer_output_viewport(
        Resource_routing::Resource_provided_by_consumer,
        m_output_key
    );

    if (
        (output_viewport.width  < 1) ||
        (output_viewport.height < 1)
    ) {
        return;
    }

    // Resize framebuffer if necessary
    igl::Dimensions color_texture_size = m_color_texture ? m_color_texture->getDimensions() : igl::Dimensions{};
    if (
        !m_color_texture ||
        (color_texture_size.width != output_viewport.width ) ||
        (color_texture_size.height != output_viewport.height)
    ) {
        log_tail->trace(
            "Resizing Texture_rendergraph_node '{}' to {} x {}",
            get_name(),
            output_viewport.width,
            output_viewport.height
        );

        //// TODO
        ////    .internal_format = m_configuration->graphics.low_hdr
        ////        ? gl::Internal_format::r11f_g11f_b10f
        ////        : gl::Internal_format::rgba16f,
        igl::IDevice& device = m_rendergraph.get_device();
        m_color_texture_multisample.reset();
        m_color_texture.reset();
        if (m_sample_count > 1) {
            igl::Result color_texture_result{};
            m_color_texture_multisample = device.createTexture(
                igl::TextureDesc{
                    .width      = static_cast<std::size_t>(output_viewport.width),
                    .height     = static_cast<std::size_t>(output_viewport.height),
                    .numSamples = static_cast<uint32_t>(m_sample_count),
                    .usage      = static_cast<igl::TextureDesc::TextureUsage>(igl::TextureDesc::TextureUsageBits::Attachment | igl::TextureDesc::TextureUsageBits::Sampled),
                    .type       = igl::TextureType::TwoD,
                    .format     = m_color_format,
                    .storage    = igl::ResourceStorage::Memoryless
                },
                &color_texture_result
            );
            assert(color_texture_result.isOk()); // TODO
            assert(m_color_texture_multisample);
        }
        igl::Result color_texture_result{};
        m_color_texture = device.createTexture(
            igl::TextureDesc{
                .width      = static_cast<std::size_t>(output_viewport.width),
                .height     = static_cast<std::size_t>(output_viewport.height),
                .numSamples = 1,
                .usage      = static_cast<igl::TextureDesc::TextureUsage>(igl::TextureDesc::TextureUsageBits::Attachment | igl::TextureDesc::TextureUsageBits::Sampled),
                .type       = igl::TextureType::TwoD,
                .format     = m_color_format,
                .storage    = igl::ResourceStorage::Private
            },
            &color_texture_result
        );
        assert(color_texture_result.isOk()); // TODO
        assert(m_color_texture);
        // TODO
        //m_color_texture->set_debug_label(
        //    fmt::format("{} Texture_rendergraph_node color texture", get_name())
        //);
        const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
        
        // TODO clear texture with clear vlue

        m_depth_stencil_texture.reset();
        if (m_depth_stencil_format != igl::TextureFormat::Invalid) {
            if (m_sample_count > 1) {
                igl::Result depth_texture_result{};
                m_depth_stencil_texture = device.createTexture(
                    igl::TextureDesc{
                        .width      = static_cast<std::size_t>(output_viewport.width),
                        .height     = static_cast<std::size_t>(output_viewport.height),
                        .numSamples = static_cast<uint32_t>(m_sample_count),
                        .usage      = static_cast<igl::TextureDesc::TextureUsage>(igl::TextureDesc::TextureUsageBits::Attachment),
                        .type       = igl::TextureType::TwoD,
                        .format     = m_depth_stencil_format,
                        .storage    = igl::ResourceStorage::Memoryless,
                        .debugName  = fmt::format("{} Texture_rendergraph_node depth-stencil renderbuffer", get_name())
                    },
                    &depth_texture_result
                );
                assert(depth_texture_result.isOk()); // TODO
                assert(m_depth_stencil_texture);
            }
            igl::Result depth_texture_result{};
            m_depth_stencil_texture = device.createTexture(
                igl::TextureDesc{
                    .width      = static_cast<std::size_t>(output_viewport.width),
                    .height     = static_cast<std::size_t>(output_viewport.height),
                    .numSamples = 1,
                    .usage      = static_cast<igl::TextureDesc::TextureUsage>(igl::TextureDesc::TextureUsageBits::Attachment),
                    .type       = igl::TextureType::TwoD,
                    .format     = m_depth_stencil_format,
                    .storage    = igl::ResourceStorage::Memoryless,
                    .debugName  = fmt::format("{} Texture_rendergraph_node depth-stencil renderbuffer", get_name())
                },
                &depth_texture_result
            );
            assert(depth_texture_result.isOk()); // TODO
            assert(m_depth_stencil_texture);
        }

        {
            igl::FramebufferDesc framebuffer_desc;
            framebuffer_desc.debugName = fmt::format("{} Texture_rendergraph_node framebuffer", get_name());
            framebuffer_desc.colorAttachments.insert({0, igl::FramebufferDesc::AttachmentDesc{m_color_texture, {}}});

            if (m_depth_stencil_texture) {
                using namespace erhe::bit;
                igl::TextureFormatProperties depth_stencil_format_properties = igl::TextureFormatProperties::fromTextureFormat(m_depth_stencil_format);
                if (test_all_rhs_bits_set(depth_stencil_format_properties.flags, static_cast<uint8_t>(igl::TextureFormatProperties::Flags::Depth))) {
                    framebuffer_desc.depthAttachment = igl::FramebufferDesc::AttachmentDesc{m_depth_stencil_texture, {}};
                }
                if (test_all_rhs_bits_set(depth_stencil_format_properties.flags, static_cast<uint8_t>(igl::TextureFormatProperties::Flags::Stencil))) {
                    framebuffer_desc.stencilAttachment = igl::FramebufferDesc::AttachmentDesc{m_depth_stencil_texture, {}};
                }
            }
            igl::Result framebuffer_result{};
            m_framebuffer = device.createFramebuffer(framebuffer_desc, &framebuffer_result);
            assert(framebuffer_result.isOk());
            assert(m_framebuffer);
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
