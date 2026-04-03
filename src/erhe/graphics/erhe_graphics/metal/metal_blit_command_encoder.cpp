#include "erhe_graphics/metal/metal_blit_command_encoder.hpp"
#include "erhe_graphics/metal/metal_buffer.hpp"
#include "erhe_graphics/metal/metal_texture.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

namespace erhe::graphics {

Blit_command_encoder_impl::Blit_command_encoder_impl(Device& device)
    : Command_encoder_impl{device}
{
}

Blit_command_encoder_impl::~Blit_command_encoder_impl() noexcept = default;

void Blit_command_encoder_impl::blit_framebuffer(const Render_pass&, glm::ivec2, glm::ivec2, const Render_pass&, glm::ivec2)
{
    ERHE_FATAL("Metal: blit_framebuffer not implemented");
}

void Blit_command_encoder_impl::copy_from_texture(const Texture*, std::uintptr_t, std::uintptr_t, glm::ivec3, glm::ivec3, const Texture*, std::uintptr_t, std::uintptr_t, glm::ivec3)
{
    ERHE_FATAL("Metal: copy_from_texture (texture to texture with regions) not implemented");
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

    Device_impl& device_impl = m_device.get_impl();
    MTL::CommandQueue* queue = device_impl.get_mtl_command_queue();
    ERHE_VERIFY(queue != nullptr);

    MTL::CommandBuffer* command_buffer = queue->commandBuffer();
    ERHE_VERIFY(command_buffer != nullptr);

    MTL::BlitCommandEncoder* blit_encoder = command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);

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

    blit_encoder->endEncoding();
    command_buffer->commit();
    command_buffer->waitUntilCompleted();
}

void Blit_command_encoder_impl::copy_from_texture(const Texture*, std::uintptr_t, std::uintptr_t, glm::ivec3, glm::ivec3, const Buffer*, std::uintptr_t, std::uintptr_t, std::uintptr_t)
{
    ERHE_FATAL("Metal: copy_from_texture (texture to buffer) not implemented");
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

    Device_impl& device_impl = m_device.get_impl();
    MTL::CommandQueue* queue = device_impl.get_mtl_command_queue();
    ERHE_VERIFY(queue != nullptr);

    MTL::CommandBuffer* command_buffer = queue->commandBuffer();
    ERHE_VERIFY(command_buffer != nullptr);

    MTL::BlitCommandEncoder* blit_encoder = command_buffer->blitCommandEncoder();
    ERHE_VERIFY(blit_encoder != nullptr);

    blit_encoder->generateMipmaps(mtl_texture);
    blit_encoder->endEncoding();
    command_buffer->commit();
    command_buffer->waitUntilCompleted();
}

void Blit_command_encoder_impl::fill_buffer(const Buffer*, std::uintptr_t, std::uintptr_t, uint8_t)
{
    ERHE_FATAL("Metal: fill_buffer not implemented");
}

void Blit_command_encoder_impl::copy_from_texture(const Texture*, std::uintptr_t, std::uintptr_t, const Texture*, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t)
{
    ERHE_FATAL("Metal: copy_from_texture (texture to texture with slices) not implemented");
}

void Blit_command_encoder_impl::copy_from_texture(const Texture*, const Texture*)
{
    ERHE_FATAL("Metal: copy_from_texture (full copy) not implemented");
}

void Blit_command_encoder_impl::copy_from_buffer(const Buffer*, std::uintptr_t, const Buffer*, std::uintptr_t, std::uintptr_t)
{
    ERHE_FATAL("Metal: copy_from_buffer (buffer to buffer) not implemented");
}

} // namespace erhe::graphics
