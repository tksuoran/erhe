#include "erhe_graphics/vulkan/vulkan_blit_command_encoder.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_verify/verify.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"

namespace erhe::graphics {

Blit_command_encoder_impl::Blit_command_encoder_impl(Device& device)
    : m_device{device}
{
}

Blit_command_encoder_impl::~Blit_command_encoder_impl() noexcept
{
}

void Blit_command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(length);
    static_cast<void>(index);
}

void Blit_command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
}

// This is the only way to copy to/from the default framebuffer. Only color buffer is supported
void Blit_command_encoder_impl::blit_framebuffer(
    const Render_pass& source_renderpass,
    glm::ivec2         source_origin,
    glm::ivec2         source_size,
    const Render_pass& destination_renderpass,
    glm::ivec2         destination_origin
)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(source_renderpass);
    static_cast<void>(source_origin);
    static_cast<void>(source_size);
    static_cast<void>(destination_renderpass);
    static_cast<void>(destination_origin);
}

// Texture to texture copy, single level, single array slice
void Blit_command_encoder_impl::copy_from_texture(
    const Texture* source_texture,
    std::uintptr_t source_slice,
    std::uintptr_t source_level,
    glm::ivec3     source_origin,
    glm::ivec3     source_size,
    const Texture* destination_texture,
    std::uintptr_t destination_slice,
    std::uintptr_t destination_level,
    glm::ivec3     destination_origin
)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(source_texture);
    static_cast<void>(source_slice);
    static_cast<void>(source_level);
    static_cast<void>(source_origin);
    static_cast<void>(source_size);
    static_cast<void>(destination_texture);
    static_cast<void>(destination_slice);
    static_cast<void>(destination_level);
    static_cast<void>(destination_origin);
}

// Buffer to texture copy
void Blit_command_encoder_impl::copy_from_buffer(
    const Buffer*  source_buffer,
    std::uintptr_t source_offset,
    std::uintptr_t source_bytes_per_row,
    std::uintptr_t source_bytes_per_image,
    glm::ivec3     source_size,
    const Texture* destination_texture,
    std::uintptr_t destination_slice,
    std::uintptr_t destination_level,
    glm::ivec3     destination_origin
)
{
    const VkBuffer vk_source_buffer     = source_buffer->get_impl().get_vk_buffer();
    const VkImage  vk_destination_image = destination_texture->get_impl().get_vk_image();
    //const erhe::dataformat::Format texture_format       = destination_texture->get_pixelformat();
    //const VkFormat                 vk_format            = to_vulkan(texture_format);

    // layout transition?
    //vkCmdPipelineBarrier();

    const VkBufferImageCopy region{
        .bufferOffset      = source_offset,
        .bufferRowLength   = static_cast<uint32_t>(source_bytes_per_row),   // in texels; if bytes, divide by pixel size
        .bufferImageHeight = static_cast<uint32_t>(source_bytes_per_image), // in rows; adjust for Vulkan semantics
        .imageSubresource  = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT, // adjust for depth/stencil if required
            .mipLevel       = static_cast<uint32_t>(destination_level),
            .baseArrayLayer = static_cast<uint32_t>(destination_slice),
            .layerCount     = 1
        },
        .imageOffset = {
            .x      = destination_origin.x,
            .y      = destination_origin.y,
            .z      = destination_origin.z
        },
        .imageExtent = {
            .width  = static_cast<uint32_t>(source_size.x),
            .height = static_cast<uint32_t>(source_size.y),
            .depth  = static_cast<uint32_t>(source_size.z)
        }
    };

    vkCmdCopyBufferToImage(
        VK_NULL_HANDLE, // TODO
        vk_source_buffer,
        vk_destination_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    // layout transition?
    ERHE_FATAL("Not implemented");
    static_cast<void>(source_buffer);
    static_cast<void>(source_offset);
    static_cast<void>(source_bytes_per_row);
    static_cast<void>(source_bytes_per_image);
    static_cast<void>(source_size);
    static_cast<void>(destination_texture);
    static_cast<void>(destination_slice);
    static_cast<void>(destination_level);
    static_cast<void>(destination_origin);
}

// Copy from texture to buffer
void Blit_command_encoder_impl::copy_from_texture(
    const Texture* source_texture,
    std::uintptr_t source_slice,
    std::uintptr_t source_level,
    glm::ivec3     source_origin,
    glm::ivec3     source_size,
    const Buffer*  destination_buffer,
    std::uintptr_t destination_offset,
    std::uintptr_t destination_bytes_per_row,
    std::uintptr_t destination_bytes_per_image
)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(source_texture);
    static_cast<void>(source_slice);
    static_cast<void>(source_level);
    static_cast<void>(source_origin);
    static_cast<void>(source_size);
    static_cast<void>(destination_buffer);
    static_cast<void>(destination_offset);
    static_cast<void>(destination_bytes_per_row);
    static_cast<void>(destination_bytes_per_image);
}

void Blit_command_encoder_impl::generate_mipmaps(const Texture* texture)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(texture);
}

void Blit_command_encoder_impl::fill_buffer(
    const Buffer*  buffer,
    std::uintptr_t offset,
    std::uintptr_t length,
    uint8_t        value
)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(length);
    static_cast<void>(value);
}

// Texture to texture copy, multiple array slices and/or mipmap levels
void Blit_command_encoder_impl::copy_from_texture(
    const Texture* source_texture,
    std::uintptr_t source_slice,
    std::uintptr_t source_level,
    const Texture* destination_texture,
    std::uintptr_t destination_slice,
    std::uintptr_t destination_level,
    std::uintptr_t slice_count,
    std::uintptr_t level_count
)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(source_texture);
    static_cast<void>(source_slice);
    static_cast<void>(source_level);
    static_cast<void>(destination_texture);
    static_cast<void>(destination_slice);
    static_cast<void>(destination_level);
    static_cast<void>(slice_count);
    static_cast<void>(level_count);
}

void Blit_command_encoder_impl::copy_from_texture(
    const Texture* source_texture,
    const Texture* destination_texture
)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(source_texture);
    static_cast<void>(destination_texture);
}

void Blit_command_encoder_impl::copy_from_buffer(
    const Buffer*  source_buffer,
    std::uintptr_t source_offset,
    const Buffer*  destination_buffer,
    std::uintptr_t destination_offset,
    std::uintptr_t size
)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(source_buffer);
    static_cast<void>(source_offset);
    static_cast<void>(destination_buffer);
    static_cast<void>(destination_offset);
    static_cast<void>(size);
}

} // namespace erhe::graphics
