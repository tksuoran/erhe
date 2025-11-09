#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

Blit_command_encoder::Blit_command_encoder(Device& device)
    : Command_encoder{device}
{
}

Blit_command_encoder::~Blit_command_encoder()
{
}

// This is the only way to copy to/from the default framebuffer. Only color buffer is supported
void Blit_command_encoder::blit_framebuffer(
    const Render_pass& source_renderpass,
    glm::ivec2         source_origin,
    glm::ivec2         source_size,
    const Render_pass& destination_renderpass,
    glm::ivec2         destination_origin
)
{
    static_cast<void>source_renderpass();
    static_cast<void>source_origin();
    static_cast<void>source_size();
    static_cast<void>destination_renderpass();
    static_cast<void>destination_origin();
}

// Texture to texture copy, single level, single array slice
void Blit_command_encoder::copy_from_texture(
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
}

// Buffer to texture copy
void Blit_command_encoder::copy_from_buffer(
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
void Blit_command_encoder::copy_from_texture(
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

void Blit_command_encoder::generate_mipmaps(const Texture* texture)
{
    static_cast<void>(texture);
}

void Blit_command_encoder::fill_buffer(
    const Buffer*  buffer,
    std::uintptr_t offset,
    std::uintptr_t length,
    uint8_t        value
)
{
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(length);
    static_cast<void>(value);
}

// Texture to texture copy, multiple array slices and/or mipmap levels
void Blit_command_encoder::copy_from_texture(
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
    static_cast<void>(source_texture);
    static_cast<void>(source_slice);
    static_cast<void>(source_level);
    static_cast<void>(destination_texture);
    static_cast<void>(destination_slice);
    static_cast<void>(destination_level);
    static_cast<void>(slice_count);
    static_cast<void>(level_count);
}

void Blit_command_encoder::copy_from_texture(
    const Texture* source_texture,
    const Texture* destination_texture
)
{
    static_cast<void>(source_texture);
    static_cast<void>(destination_texture);
}

void Blit_command_encoder::copy_from_buffer(
    const Buffer*  source_buffer,
    std::uintptr_t source_offset,
    const Buffer*  destination_buffer,
    std::uintptr_t destination_offset,
    std::uintptr_t size
)
{
    static_cast<void>(source_buffer);
    static_cast<void>(source_offset);
    static_cast<void>(destination_buffer);
    static_cast<void>(destination_offset);
    static_cast<void>(size);
}

} // namespace erhe::graphics
