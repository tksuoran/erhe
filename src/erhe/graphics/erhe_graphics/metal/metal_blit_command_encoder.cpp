#include "erhe_graphics/metal/metal_blit_command_encoder.hpp"
#include "erhe_graphics/metal/metal_buffer.hpp"
#include "erhe_graphics/metal/metal_texture.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

#include <mutex>

namespace erhe::graphics {

namespace {

// RAII helper used by each blit method. Prefers recording into the
// active device-frame cb (so the blit lands in the single per-frame
// commit). Falls back to a one-shot cb when no device frame is active
// (init-time callers). The destructor commits + waits only in the
// fallback case; device-frame recordings are committed by end_frame().
class Recording_scope final
{
public:
    explicit Recording_scope(Device_impl& device_impl)
        : m_device_impl{device_impl}
    {
        MTL::CommandBuffer* active = device_impl.get_device_frame_command_buffer();
        if (active != nullptr) {
            m_device_lock = std::unique_lock<std::mutex>{device_impl.m_recording_mutex};
            m_command_buffer = active;
            return;
        }
        MTL::CommandQueue* queue = device_impl.get_mtl_command_queue();
        ERHE_VERIFY(queue != nullptr);
        m_command_buffer = queue->commandBuffer();
        ERHE_VERIFY(m_command_buffer != nullptr);
        m_owns_command_buffer = true;
    }
    ~Recording_scope() noexcept
    {
        if (m_owns_command_buffer && (m_command_buffer != nullptr)) {
            m_command_buffer->commit();
            m_command_buffer->waitUntilCompleted();
        }
    }

    // Apply waitForFence/updateFence on a blit encoder when recording
    // into the device cb. No-ops for the one-shot cb fallback -- that
    // cb is independently committed+waited by the destructor and does
    // not need to sync against device-cb encoders.
    void wait_frame_fence(MTL::BlitCommandEncoder* encoder) const
    {
        if (m_owns_command_buffer || (encoder == nullptr)) {
            return;
        }
        MTL::Fence* fence = m_device_impl.get_device_frame_fence();
        if (fence != nullptr) {
            encoder->waitForFence(fence);
        }
    }
    void update_frame_fence(MTL::BlitCommandEncoder* encoder) const
    {
        if (m_owns_command_buffer || (encoder == nullptr)) {
            return;
        }
        MTL::Fence* fence = m_device_impl.get_device_frame_fence();
        if (fence != nullptr) {
            encoder->updateFence(fence);
        }
    }
    Recording_scope(const Recording_scope&)            = delete;
    Recording_scope& operator=(const Recording_scope&) = delete;
    Recording_scope(Recording_scope&&)                 = delete;
    Recording_scope& operator=(Recording_scope&&)      = delete;

    MTL::CommandBuffer*          m_command_buffer{nullptr};

private:
    Device_impl&                 m_device_impl;
    std::unique_lock<std::mutex> m_device_lock{};
    bool                         m_owns_command_buffer{false};
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

void Blit_command_encoder_impl::copy_from_texture(
    const Texture* const source_texture,
    const std::uintptr_t source_slice,
    const std::uintptr_t source_level,
    const glm::ivec3     source_origin,
    const glm::ivec3     source_size,
    const Texture* const destination_texture,
    const std::uintptr_t destination_slice,
    const std::uintptr_t destination_level,
    const glm::ivec3     destination_origin
)
{
    if ((source_texture == nullptr) || (destination_texture == nullptr)) {
        return;
    }
    MTL::Texture* src_mtl_texture = source_texture->get_impl().get_mtl_texture();
    MTL::Texture* dst_mtl_texture = destination_texture->get_impl().get_mtl_texture();
    if ((src_mtl_texture == nullptr) || (dst_mtl_texture == nullptr)) {
        return;
    }

    Recording_scope scope{m_device.get_impl()};
    MTL::BlitCommandEncoder* blit_encoder = scope.m_command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);
    scope.wait_frame_fence(blit_encoder);

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

    scope.update_frame_fence(blit_encoder);
    blit_encoder->endEncoding();
}

void Blit_command_encoder_impl::copy_from_buffer(
    const Buffer* const  source_buffer,
    const std::uintptr_t source_offset,
    const std::uintptr_t source_bytes_per_row,
    const std::uintptr_t source_bytes_per_image,
    const glm::ivec3     source_size,
    const Texture* const destination_texture,
    const std::uintptr_t destination_slice,
    const std::uintptr_t destination_level,
    const glm::ivec3     destination_origin
)
{
    if ((source_buffer == nullptr) || (destination_texture == nullptr)) {
        return;
    }
    MTL::Buffer*  mtl_buffer  = source_buffer->get_impl().get_mtl_buffer();
    MTL::Texture* mtl_texture = destination_texture->get_impl().get_mtl_texture();
    if ((mtl_buffer == nullptr) || (mtl_texture == nullptr)) {
        return;
    }

    Recording_scope scope{m_device.get_impl()};
    MTL::BlitCommandEncoder* blit_encoder = scope.m_command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);
    scope.wait_frame_fence(blit_encoder);

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

    scope.update_frame_fence(blit_encoder);
    blit_encoder->endEncoding();
}

void Blit_command_encoder_impl::copy_from_texture(
    const Texture* const source_texture,
    const std::uintptr_t source_slice,
    const std::uintptr_t source_level,
    const glm::ivec3     source_origin,
    const glm::ivec3     source_size,
    const Buffer* const  destination_buffer,
    const std::uintptr_t destination_offset,
    const std::uintptr_t destination_bytes_per_row,
    const std::uintptr_t destination_bytes_per_image
)
{
    if ((source_texture == nullptr) || (destination_buffer == nullptr)) {
        return;
    }
    MTL::Texture* mtl_texture = source_texture->get_impl().get_mtl_texture();
    MTL::Buffer*  mtl_buffer  = destination_buffer->get_impl().get_mtl_buffer();
    if ((mtl_texture == nullptr) || (mtl_buffer == nullptr)) {
        return;
    }

    Recording_scope scope{m_device.get_impl()};
    MTL::BlitCommandEncoder* blit_encoder = scope.m_command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);
    scope.wait_frame_fence(blit_encoder);

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

    scope.update_frame_fence(blit_encoder);
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

    Recording_scope scope{m_device.get_impl()};
    MTL::BlitCommandEncoder* blit_encoder = scope.m_command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);
    scope.wait_frame_fence(blit_encoder);

    blit_encoder->generateMipmaps(mtl_texture);
    scope.update_frame_fence(blit_encoder);
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

    Recording_scope scope{m_device.get_impl()};
    MTL::BlitCommandEncoder* blit_encoder = scope.m_command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);
    scope.wait_frame_fence(blit_encoder);

    blit_encoder->fillBuffer(
        mtl_buffer,
        NS::Range(static_cast<NS::UInteger>(offset), static_cast<NS::UInteger>(length)),
        value
    );

    scope.update_frame_fence(blit_encoder);
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
                source_texture,
                source_slice + slice,
                source_level + level,
                glm::ivec3{0, 0, 0},
                glm::ivec3{width, height, 1},
                destination_texture,
                destination_slice + slice,
                destination_level + level,
                glm::ivec3{0, 0, 0}
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

    Recording_scope scope{m_device.get_impl()};
    MTL::BlitCommandEncoder* blit_encoder = scope.m_command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);
    scope.wait_frame_fence(blit_encoder);

    blit_encoder->copyFromBuffer(
        src_mtl_buffer,
        static_cast<NS::UInteger>(source_offset),
        dst_mtl_buffer,
        static_cast<NS::UInteger>(destination_offset),
        static_cast<NS::UInteger>(size)
    );

    scope.update_frame_fence(blit_encoder);
    blit_encoder->endEncoding();
}

} // namespace erhe::graphics
