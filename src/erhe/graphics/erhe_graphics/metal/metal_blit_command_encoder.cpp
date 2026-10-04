#include "erhe_graphics/metal/metal_blit_command_encoder.hpp"
#include "erhe_graphics/metal/metal_buffer.hpp"
#include "erhe_graphics/metal/metal_command_buffer.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_texture.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

namespace {

// RAII helper used by each blit method. Records into the cb's
// MTL::CommandBuffer and applies waitForFence/updateFence on the cb's
// inter-encoder fence so successive encoders on the same cb stay
// ordered against the blit (Metal does not always track aliased view
// hazards automatically). The cb is owned by the caller's
// Command_buffer; this scope never commits.
class Recording_scope final
{
public:
    explicit Recording_scope(Command_buffer& command_buffer)
    {
        Command_buffer_impl& cb_impl = command_buffer.get_impl();
        m_command_buffer       = cb_impl.get_mtl_command_buffer();
        m_inter_encoder_fence  = cb_impl.get_inter_encoder_fence();
        ERHE_VERIFY(m_command_buffer != nullptr);
    }
    ~Recording_scope() noexcept = default;
    Recording_scope(const Recording_scope&)            = delete;
    Recording_scope& operator=(const Recording_scope&) = delete;
    Recording_scope(Recording_scope&&)                 = delete;
    Recording_scope& operator=(Recording_scope&&)      = delete;

    void wait_inter_encoder_fence(MTL::BlitCommandEncoder* encoder) const
    {
        if ((encoder != nullptr) && (m_inter_encoder_fence != nullptr)) {
            encoder->waitForFence(m_inter_encoder_fence);
        }
    }
    void update_inter_encoder_fence(MTL::BlitCommandEncoder* encoder) const
    {
        if ((encoder != nullptr) && (m_inter_encoder_fence != nullptr)) {
            encoder->updateFence(m_inter_encoder_fence);
        }
    }

    MTL::CommandBuffer* m_command_buffer{nullptr};

private:
    MTL::Fence*         m_inter_encoder_fence{nullptr};
};

} // namespace

Blit_command_encoder_impl::Blit_command_encoder_impl(Device& device, Command_buffer& command_buffer)
    : Command_encoder_impl{device, command_buffer}
{
}

Blit_command_encoder_impl::~Blit_command_encoder_impl() noexcept = default;

void Blit_command_encoder_impl::blit_framebuffer(const Render_pass&, glm::ivec2, glm::ivec2, const Render_pass&, glm::ivec2)
{
    ERHE_FATAL("Metal: blit_framebuffer not implemented");
}

void Blit_command_encoder_impl::copy_from_texture(const Texture_location& source, const glm::ivec3 source_size, const Texture_location& destination)
{
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

    Recording_scope scope{m_command_buffer};
    MTL::BlitCommandEncoder* blit_encoder = scope.m_command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);
    scope.wait_inter_encoder_fence(blit_encoder);

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

    scope.update_inter_encoder_fence(blit_encoder);
    blit_encoder->endEncoding();
}

void Blit_command_encoder_impl::copy_from_buffer(const Buffer_texel_location& source, const glm::ivec3 source_size, const Texture_location& destination)
{
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

    Recording_scope scope{m_command_buffer};
    MTL::BlitCommandEncoder* blit_encoder = scope.m_command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);
    scope.wait_inter_encoder_fence(blit_encoder);

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

    scope.update_inter_encoder_fence(blit_encoder);
    blit_encoder->endEncoding();
}

void Blit_command_encoder_impl::copy_from_texture(const Texture_location& source, const glm::ivec3 source_size, const Buffer_texel_location& destination)
{
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

    Recording_scope scope{m_command_buffer};
    MTL::BlitCommandEncoder* blit_encoder = scope.m_command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);
    scope.wait_inter_encoder_fence(blit_encoder);

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

    scope.update_inter_encoder_fence(blit_encoder);
    blit_encoder->endEncoding();
}

void Blit_command_encoder_impl::generate_mipmaps(const Texture* texture)
{
    if (texture == nullptr) {
        return;
    }
    MTL::Texture* mtl_texture = texture->get_impl().get_mtl_texture();
    if (mtl_texture == nullptr) {
        return;
    }

    Recording_scope scope{m_command_buffer};
    MTL::BlitCommandEncoder* blit_encoder = scope.m_command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);
    scope.wait_inter_encoder_fence(blit_encoder);

    blit_encoder->generateMipmaps(mtl_texture);
    scope.update_inter_encoder_fence(blit_encoder);
    blit_encoder->endEncoding();
}

void Blit_command_encoder_impl::fill_buffer(
    const Buffer* const  buffer,
    const std::uintptr_t offset,
    const std::uintptr_t length,
    const uint8_t        value
)
{
    if (buffer == nullptr) {
        return;
    }
    MTL::Buffer* mtl_buffer = buffer->get_impl().get_mtl_buffer();
    if (mtl_buffer == nullptr) {
        return;
    }

    Recording_scope scope{m_command_buffer};
    MTL::BlitCommandEncoder* blit_encoder = scope.m_command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);
    scope.wait_inter_encoder_fence(blit_encoder);

    blit_encoder->fillBuffer(
        mtl_buffer,
        NS::Range(static_cast<NS::UInteger>(offset), static_cast<NS::UInteger>(length)),
        value
    );

    scope.update_inter_encoder_fence(blit_encoder);
    blit_encoder->endEncoding();
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
    if ((source_buffer == nullptr) || (destination_buffer == nullptr)) {
        return;
    }
    MTL::Buffer* src_mtl_buffer = source_buffer->get_impl().get_mtl_buffer();
    MTL::Buffer* dst_mtl_buffer = destination_buffer->get_impl().get_mtl_buffer();
    if ((src_mtl_buffer == nullptr) || (dst_mtl_buffer == nullptr)) {
        return;
    }

    Recording_scope scope{m_command_buffer};
    MTL::BlitCommandEncoder* blit_encoder = scope.m_command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);
    scope.wait_inter_encoder_fence(blit_encoder);

    blit_encoder->copyFromBuffer(
        src_mtl_buffer,
        static_cast<NS::UInteger>(source_offset),
        dst_mtl_buffer,
        static_cast<NS::UInteger>(destination_offset),
        static_cast<NS::UInteger>(size)
    );

    scope.update_inter_encoder_fence(blit_encoder);
    blit_encoder->endEncoding();
}

} // namespace erhe::graphics
