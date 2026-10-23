#include "erhe_graphics/metal4/metal4_blit_command_encoder.hpp"
#include "erhe_graphics/metal4/metal4_buffer.hpp"
#include "erhe_graphics/metal4/metal4_command_buffer.hpp"
#include "erhe_graphics/metal4/metal4_device.hpp"
#include "erhe_graphics/metal4/metal4_submission.hpp"
#include "erhe_graphics/metal4/metal4_swapchain.hpp"
#include "erhe_graphics/metal4/metal4_texture.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>

namespace erhe::graphics {

namespace {

// What a blit accesses: a texture (the Vulkan blit encoder transitions it
// from its tracked layout to the transfer layout and back to a shader
// readable one), or buffers only (Vulkan records no barrier: the
// application's memory_barrier / buffer_barrier orders them).
enum class Blit_access : unsigned int {
    texture,
    buffers
};

// RAII helper used by each blit method: one MTL4 compute encoder (blits
// are compute encoder commands under MTL4) on the cb's MTL4::CommandBuffer,
// ended when the scope ends. A texture blit is ordered as Vulkan's layout
// transitions order it: every earlier access before the blit (the
// transition out of the tracked layout covers whatever accessed it) - a
// consumer barrier at the start of the encoder - and the blit before every
// later access (the transition back) - a producer barrier before its end.
// The cb is owned by the
// caller's Command_buffer; this scope never commits.
class Recording_scope final
{
public:
    Recording_scope(Command_buffer& command_buffer, const Blit_access access)
        : m_command_buffer_impl{command_buffer.get_impl()}
        , m_access             {access}
    {
        MTL4::CommandBuffer* const mtl_command_buffer = m_command_buffer_impl.get_mtl4_command_buffer();
        ERHE_VERIFY(mtl_command_buffer != nullptr);
        m_encoder = mtl_command_buffer->computeCommandEncoder();
        ERHE_VERIFY(m_encoder != nullptr);
        if (m_access == Blit_access::texture) {
            m_command_buffer_impl.encode_start_barriers(m_encoder, c_all_stages, c_stage_blit);
        } else {
            m_command_buffer_impl.encode_start_barriers(m_encoder);
        }
    }
    ~Recording_scope() noexcept
    {
        if (m_access == Blit_access::texture) {
            encode_producer_barrier(m_encoder, c_stage_blit, c_all_stages);
        }
        m_encoder->endEncoding();
    }
    Recording_scope(const Recording_scope&)            = delete;
    Recording_scope& operator=(const Recording_scope&) = delete;
    Recording_scope(Recording_scope&&)                 = delete;
    Recording_scope& operator=(Recording_scope&&)      = delete;

    Command_buffer_impl&         m_command_buffer_impl;
    Blit_access                  m_access;
    MTL4::ComputeCommandEncoder* m_encoder{nullptr};
};

} // namespace

Blit_command_encoder_impl::Blit_command_encoder_impl(Device& device, Command_buffer& command_buffer)
    : Command_encoder_impl{device, command_buffer}
{
}

Blit_command_encoder_impl::~Blit_command_encoder_impl() noexcept = default;

namespace {

// Color attachment 0 of a render pass as the blit sees it: the current
// drawable (or emulated ring) texture of a swapchain render pass, otherwise
// the attachment texture at its level and layer.
class Blit_attachment
{
public:
    MTL::Texture* texture{nullptr};
    NS::UInteger  level  {0};
    NS::UInteger  slice  {0};
};

[[nodiscard]] auto get_blit_attachment(const Render_pass& render_pass) -> Blit_attachment
{
    Swapchain* const swapchain = render_pass.get_swapchain();
    if (swapchain != nullptr) {
        return Blit_attachment{.texture = swapchain->get_impl().get_current_color_texture()};
    }
    const Render_pass_attachment_descriptor& attachment = render_pass.get_descriptor().color_attachments[0];
    return Blit_attachment{
        .texture = (attachment.texture != nullptr) ? attachment.texture->get_impl().get_mtl_texture() : nullptr,
        .level   = static_cast<NS::UInteger>(attachment.texture_level),
        .slice   = static_cast<NS::UInteger>(attachment.texture_layer)
    };
}

[[nodiscard]] auto get_level_extent(const Blit_attachment& attachment) -> glm::ivec2
{
    return glm::ivec2{
        std::max(1, static_cast<int>(attachment.texture->width ()) >> static_cast<int>(attachment.level)),
        std::max(1, static_cast<int>(attachment.texture->height()) >> static_cast<int>(attachment.level))
    };
}

} // anonymous namespace

void Blit_command_encoder_impl::blit_framebuffer(
    const Render_pass& source_renderpass,
    const glm::ivec2   source_origin,
    const glm::ivec2   source_size,
    const Render_pass& destination_renderpass,
    const glm::ivec2   destination_origin
)
{
    Scoped_transient_object_pool object_pool{}; // drains the encoders, descriptor and other autoreleased objects created here

    // A swapchain render pass needs its frame's drawable: blit after
    // begin_swapchain, before the frame is submitted. Its first use commits
    // the work recorded before it and takes the drawable.
    for (const Render_pass* const render_pass : { &source_renderpass, &destination_renderpass }) {
        Swapchain* const swapchain = render_pass->get_swapchain();
        if (swapchain != nullptr) {
            m_command_buffer.get_impl().acquire_drawable_for_use(swapchain->get_impl());
        }
    }
    const Blit_attachment source      = get_blit_attachment(source_renderpass);
    const Blit_attachment destination = get_blit_attachment(destination_renderpass);
    ERHE_VERIFY((source.texture != nullptr) && (destination.texture != nullptr));
    ERHE_VERIFY((source.texture->sampleCount() == 1) && (destination.texture->sampleCount() == 1));

    const Blit_framebuffer_region region = clip_blit_framebuffer_region(
        source_origin,
        source_size,
        get_level_extent(source),
        destination_origin,
        get_level_extent(destination)
    );
    if ((region.size.x <= 0) || (region.size.y <= 0)) {
        return;
    }

    Command_buffer_impl&       cb_impl            = m_command_buffer.get_impl();
    MTL4::CommandBuffer* const mtl_command_buffer = cb_impl.get_mtl4_command_buffer();
    ERHE_VERIFY(mtl_command_buffer != nullptr);

    // Same format: a texel copy.
    if (source.texture->pixelFormat() == destination.texture->pixelFormat()) {
        Recording_scope scope{m_command_buffer, Blit_access::texture};
        scope.m_encoder->copyFromTexture(
            source.texture, source.slice, source.level,
            MTL::Origin{static_cast<NS::UInteger>(region.source_origin.x), static_cast<NS::UInteger>(region.source_origin.y), 0},
            MTL::Size  {static_cast<NS::UInteger>(region.size.x),          static_cast<NS::UInteger>(region.size.y),          1},
            destination.texture, destination.slice, destination.level,
            MTL::Origin{static_cast<NS::UInteger>(region.destination_origin.x), static_cast<NS::UInteger>(region.destination_origin.y), 0}
        );
        return;
    }

    // Different formats: Metal copies convert nothing, so draw the
    // rectangle with a pipeline whose color attachment converts.
    MTL::RenderPipelineState* const pipeline = m_device.get_impl().get_blit_framebuffer_pipeline(
        static_cast<unsigned long>(destination.texture->pixelFormat())
    );
    ERHE_VERIFY(pipeline != nullptr);

    // A 2D view of the one source subresource: the shader reads
    // texture2d, whatever the source's type, level count or layer count.
    MTL::Texture* const source_view = source.texture->newTextureView(
        source.texture->pixelFormat(),
        MTL::TextureType2D,
        NS::Range{source.level, 1},
        NS::Range{source.slice, 1}
    );
    ERHE_VERIFY(source_view != nullptr);

    MTL4::RenderPassDescriptor* const pass_descriptor = MTL4::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* const color_attachment = pass_descriptor->colorAttachments()->object(0);
    color_attachment->setTexture    (destination.texture);
    color_attachment->setLevel      (destination.level);
    color_attachment->setSlice      (destination.slice);
    color_attachment->setLoadAction (MTL::LoadActionLoad);
    color_attachment->setStoreAction(MTL::StoreActionStore);

    // As a texture blit (Recording_scope), recorded as a render encoder:
    // every earlier access before the draw, the draw before every later one.
    MTL4::RenderCommandEncoder* const render_encoder = mtl_command_buffer->renderCommandEncoder(pass_descriptor);
    pass_descriptor->release();
    ERHE_VERIFY(render_encoder != nullptr);
    cb_impl.encode_start_barriers(render_encoder, Command_buffer_impl::Render_encoder_use::transfer, c_all_stages, c_render_encoder_stages);
    render_encoder->setRenderPipelineState(pipeline);
    render_encoder->setViewport(
        MTL::Viewport{
            static_cast<double>(region.destination_origin.x), static_cast<double>(region.destination_origin.y),
            static_cast<double>(region.size.x),               static_cast<double>(region.size.y),
            0.0, 1.0
        }
    );
    render_encoder->setScissorRect(
        MTL::ScissorRect{
            static_cast<NS::UInteger>(region.destination_origin.x), static_cast<NS::UInteger>(region.destination_origin.y),
            static_cast<NS::UInteger>(region.size.x),               static_cast<NS::UInteger>(region.size.y)
        }
    );
    const glm::ivec2 offset = region.source_origin - region.destination_origin;
    MTL4::ArgumentTable* const argument_table = cb_impl.acquire_argument_table();
    argument_table->setTexture(source_view->gpuResourceID(), 0);
    argument_table->setAddress(cb_impl.allocate_inline_bytes(&offset, sizeof(offset)), 0);
    render_encoder->setArgumentTable(argument_table, MTL::RenderStageVertex | MTL::RenderStageFragment);
    render_encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger{0}, NS::UInteger{3});
    encode_producer_barrier(render_encoder, c_render_encoder_stages, c_all_stages);
    render_encoder->endEncoding();

    // MTL4 command buffers retain nothing: the view lives until the GPU has
    // completed the cb.
    cb_impl.retain_until_completed(source_view);
}

void Blit_command_encoder_impl::copy_from_texture(const Texture_location& source, const glm::ivec3 source_size, const Texture_location& destination)
{
    Scoped_transient_object_pool object_pool{}; // drains the encoder and other autoreleased objects created here
    const Texture* const source_texture      = source.texture;
    const std::uintptr_t source_slice        = source.slice;
    const std::uintptr_t source_level        = source.level;
    const glm::ivec3     source_origin       = source.origin;
    const Texture* const destination_texture = destination.texture;
    const std::uintptr_t destination_slice   = destination.slice;
    const std::uintptr_t destination_level   = destination.level;
    const glm::ivec3     destination_origin  = destination.origin;
    if ((source_texture == nullptr) || (destination_texture == nullptr)) {
        return;
    }
    MTL::Texture* src_mtl_texture = source_texture->get_impl().get_mtl_texture();
    MTL::Texture* dst_mtl_texture = destination_texture->get_impl().get_mtl_texture();
    if ((src_mtl_texture == nullptr) || (dst_mtl_texture == nullptr)) {
        return;
    }

    Recording_scope scope{m_command_buffer, Blit_access::texture};
    MTL4::ComputeCommandEncoder* const blit_encoder = scope.m_encoder;

    blit_encoder->copyFromTexture(
        src_mtl_texture,
        static_cast<NS::UInteger>(source_slice),
        static_cast<NS::UInteger>(source_level),
        MTL::Origin(
            static_cast<NS::UInteger>(source_origin.x),
            static_cast<NS::UInteger>(source_origin.y),
            static_cast<NS::UInteger>(source_origin.z)
        ),
        MTL::Size(
            static_cast<NS::UInteger>(source_size.x),
            static_cast<NS::UInteger>(source_size.y),
            static_cast<NS::UInteger>(source_size.z)
        ),
        dst_mtl_texture,
        static_cast<NS::UInteger>(destination_slice),
        static_cast<NS::UInteger>(destination_level),
        MTL::Origin(
            static_cast<NS::UInteger>(destination_origin.x),
            static_cast<NS::UInteger>(destination_origin.y),
            static_cast<NS::UInteger>(destination_origin.z)
        )
    );

}

void Blit_command_encoder_impl::copy_from_buffer(const Buffer_texel_location& source, const glm::ivec3 source_size, const Texture_location& destination)
{
    Scoped_transient_object_pool object_pool{}; // drains the encoder and other autoreleased objects created here
    const Buffer* const  source_buffer          = source.buffer;
    const std::uintptr_t source_offset          = source.offset;
    const std::uintptr_t source_bytes_per_row   = source.bytes_per_row;
    const std::uintptr_t source_bytes_per_image = source.bytes_per_image;
    const Texture* const destination_texture    = destination.texture;
    const std::uintptr_t destination_slice      = destination.slice;
    const std::uintptr_t destination_level      = destination.level;
    const glm::ivec3     destination_origin     = destination.origin;
    if ((source_buffer == nullptr) || (destination_texture == nullptr)) {
        return;
    }
    MTL::Buffer*  mtl_buffer  = source_buffer->get_impl().get_mtl_buffer();
    MTL::Texture* mtl_texture = destination_texture->get_impl().get_mtl_texture();
    if ((mtl_buffer == nullptr) || (mtl_texture == nullptr)) {
        return;
    }

    Recording_scope scope{m_command_buffer, Blit_access::texture};
    MTL4::ComputeCommandEncoder* const blit_encoder = scope.m_encoder;

    blit_encoder->copyFromBuffer(
        mtl_buffer,
        static_cast<NS::UInteger>(source_offset),
        static_cast<NS::UInteger>(source_bytes_per_row),
        static_cast<NS::UInteger>(source_bytes_per_image),
        MTL::Size(
            static_cast<NS::UInteger>(source_size.x),
            static_cast<NS::UInteger>(source_size.y),
            static_cast<NS::UInteger>(source_size.z)
        ),
        mtl_texture,
        static_cast<NS::UInteger>(destination_slice),
        static_cast<NS::UInteger>(destination_level),
        MTL::Origin(
            static_cast<NS::UInteger>(destination_origin.x),
            static_cast<NS::UInteger>(destination_origin.y),
            static_cast<NS::UInteger>(destination_origin.z)
        )
    );

}

void Blit_command_encoder_impl::copy_from_texture(const Texture_location& source, const glm::ivec3 source_size, const Buffer_texel_location& destination)
{
    Scoped_transient_object_pool object_pool{}; // drains the encoder and other autoreleased objects created here
    const Texture* const source_texture              = source.texture;
    const std::uintptr_t source_slice                = source.slice;
    const std::uintptr_t source_level                = source.level;
    const glm::ivec3     source_origin               = source.origin;
    const Buffer* const  destination_buffer          = destination.buffer;
    const std::uintptr_t destination_offset          = destination.offset;
    const std::uintptr_t destination_bytes_per_row   = destination.bytes_per_row;
    const std::uintptr_t destination_bytes_per_image = destination.bytes_per_image;
    if ((source_texture == nullptr) || (destination_buffer == nullptr)) {
        return;
    }
    MTL::Texture* mtl_texture = source_texture->get_impl().get_mtl_texture();
    MTL::Buffer*  mtl_buffer  = destination_buffer->get_impl().get_mtl_buffer();
    if ((mtl_texture == nullptr) || (mtl_buffer == nullptr)) {
        return;
    }

    Recording_scope scope{m_command_buffer, Blit_access::texture};
    MTL4::ComputeCommandEncoder* const blit_encoder = scope.m_encoder;

    blit_encoder->copyFromTexture(
        mtl_texture,
        static_cast<NS::UInteger>(source_slice),
        static_cast<NS::UInteger>(source_level),
        MTL::Origin(
            static_cast<NS::UInteger>(source_origin.x),
            static_cast<NS::UInteger>(source_origin.y),
            static_cast<NS::UInteger>(source_origin.z)
        ),
        MTL::Size(
            static_cast<NS::UInteger>(source_size.x),
            static_cast<NS::UInteger>(source_size.y),
            static_cast<NS::UInteger>(source_size.z)
        ),
        mtl_buffer,
        static_cast<NS::UInteger>(destination_offset),
        static_cast<NS::UInteger>(destination_bytes_per_row),
        static_cast<NS::UInteger>(destination_bytes_per_image)
    );

}

void Blit_command_encoder_impl::generate_mipmaps(const Texture* texture)
{
    Scoped_transient_object_pool object_pool{}; // drains the encoder and other autoreleased objects created here
    if (texture == nullptr) {
        return;
    }
    MTL::Texture* mtl_texture = texture->get_impl().get_mtl_texture();
    if (mtl_texture == nullptr) {
        return;
    }

    Recording_scope scope{m_command_buffer, Blit_access::texture};
    MTL4::ComputeCommandEncoder* const blit_encoder = scope.m_encoder;

    blit_encoder->generateMipmaps(mtl_texture);
}

void Blit_command_encoder_impl::fill_buffer(
    const Buffer* const  buffer,
    const std::uintptr_t offset,
    const std::uintptr_t length,
    const uint8_t        value
)
{
    Scoped_transient_object_pool object_pool{}; // drains the encoder and other autoreleased objects created here
    if (buffer == nullptr) {
        return;
    }
    MTL::Buffer* mtl_buffer = buffer->get_impl().get_mtl_buffer();
    if (mtl_buffer == nullptr) {
        return;
    }

    Recording_scope scope{m_command_buffer, Blit_access::buffers};
    MTL4::ComputeCommandEncoder* const blit_encoder = scope.m_encoder;

    blit_encoder->fillBuffer(
        mtl_buffer,
        NS::Range(static_cast<NS::UInteger>(offset), static_cast<NS::UInteger>(length)),
        value
    );

}

void Blit_command_encoder_impl::copy_from_texture(
    const Texture* const source_texture,
    const std::uintptr_t source_slice,
    const std::uintptr_t source_level,
    const Texture* const destination_texture,
    const std::uintptr_t destination_slice,
    const std::uintptr_t destination_level,
    const std::uintptr_t slice_count,
    const std::uintptr_t level_count
)
{
    if ((source_texture == nullptr) || (destination_texture == nullptr)) {
        return;
    }

    for (std::uintptr_t slice = 0; slice < slice_count; ++slice) {
        for (std::uintptr_t level = 0; level < level_count; ++level) {
            int width  = source_texture->get_width(static_cast<unsigned int>(source_level + level));
            int height = source_texture->get_height(static_cast<unsigned int>(source_level + level));
            if ((width <= 0) || (height <= 0)) {
                continue;
            }
            copy_from_texture(
                erhe::graphics::Texture_location{.texture = source_texture, .slice = source_slice + slice, .level = source_level + level, .origin = glm::ivec3{0, 0, 0}},
                glm::ivec3{width, height, 1},
                erhe::graphics::Texture_location{.texture = destination_texture, .slice = destination_slice + slice, .level = destination_level + level, .origin = glm::ivec3{0, 0, 0}}
            );
        }
    }
}

void Blit_command_encoder_impl::copy_from_texture(
    const Texture* const source_texture,
    const Texture* const destination_texture
)
{
    if ((source_texture == nullptr) || (destination_texture == nullptr)) {
        return;
    }

    int level_count = source_texture->get_level_count();
    int layer_count = source_texture->get_array_layer_count();
    if (layer_count < 1) {
        layer_count = 1;
    }

    copy_from_texture(
        source_texture, 0, 0,
        destination_texture, 0, 0,
        static_cast<std::uintptr_t>(layer_count),
        static_cast<std::uintptr_t>(level_count)
    );
}

void Blit_command_encoder_impl::copy_from_buffer(
    const Buffer* const  source_buffer,
    const std::uintptr_t source_offset,
    const Buffer* const  destination_buffer,
    const std::uintptr_t destination_offset,
    const std::uintptr_t size
)
{
    Scoped_transient_object_pool object_pool{}; // drains the encoder and other autoreleased objects created here
    if ((source_buffer == nullptr) || (destination_buffer == nullptr)) {
        return;
    }
    MTL::Buffer* src_mtl_buffer = source_buffer->get_impl().get_mtl_buffer();
    MTL::Buffer* dst_mtl_buffer = destination_buffer->get_impl().get_mtl_buffer();
    if ((src_mtl_buffer == nullptr) || (dst_mtl_buffer == nullptr)) {
        return;
    }

    Recording_scope scope{m_command_buffer, Blit_access::buffers};
    MTL4::ComputeCommandEncoder* const blit_encoder = scope.m_encoder;

    blit_encoder->copyFromBuffer(
        src_mtl_buffer,
        static_cast<NS::UInteger>(source_offset),
        dst_mtl_buffer,
        static_cast<NS::UInteger>(destination_offset),
        static_cast<NS::UInteger>(size)
    );

}

} // namespace erhe::graphics
