#include "erhe_graphics/null/null_blit_command_encoder.hpp"

namespace erhe::graphics {

Blit_command_encoder_impl::Blit_command_encoder_impl(Device& device)
    : Command_encoder_impl{device}
{
}

Blit_command_encoder_impl::~Blit_command_encoder_impl() noexcept = default;

void Blit_command_encoder_impl::blit_framebuffer(
    const Render_pass& source_renderpass,
    glm::ivec2         source_origin,
    glm::ivec2         source_size,
    const Render_pass& destination_renderpass,
    glm::ivec2         destination_origin
)
{
    static_cast<void>(source_renderpass);
    static_cast<void>(source_origin);
    static_cast<void>(source_size);
    static_cast<void>(destination_renderpass);
    static_cast<void>(destination_origin);
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
    static_cast<void>(texture);
}

void Blit_command_encoder_impl::fill_buffer(
    const Buffer* const  buffer,
    const std::uintptr_t offset,
    const std::uintptr_t length,
    const uint8_t        value
)
{
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(length);
    static_cast<void>(value);
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
    const Texture* const source_texture,
    const Texture* const destination_texture
)
{
    static_cast<void>(source_texture);
    static_cast<void>(destination_texture);
}

void Blit_command_encoder_impl::copy_from_buffer(
    const Buffer* const  source_buffer,
    const std::uintptr_t source_offset,
    const Buffer* const  destination_buffer,
    const std::uintptr_t destination_offset,
    const std::uintptr_t size
)
{
    static_cast<void>(source_buffer);
    static_cast<void>(source_offset);
    static_cast<void>(destination_buffer);
    static_cast<void>(destination_offset);
    static_cast<void>(size);
}

} // namespace erhe::graphics
