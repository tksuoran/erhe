#include "erhe_graphics/blit_command_encoder.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_blit_command_encoder.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_blit_command_encoder.hpp"
#endif

namespace erhe::graphics {

Blit_command_encoder::Blit_command_encoder(Device& device)
    : m_impl{std::make_unique<Blit_command_encoder_impl>(device)}
{
}
Blit_command_encoder::~Blit_command_encoder() noexcept
{
}
void Blit_command_encoder::set_buffer(
    Buffer_target  buffer_target,
    const Buffer*  buffer,
    std::uintptr_t offset,
    std::uintptr_t length,
    std::uintptr_t index
)
{
    m_impl->set_buffer(buffer_target, buffer, offset, length, index);
}
void Blit_command_encoder::set_buffer(
    Buffer_target buffer_target,
    const Buffer* buffer
)
{
    m_impl->set_buffer(buffer_target, buffer);
}
void Blit_command_encoder::blit_framebuffer(
    const Render_pass& source_renderpass,
    glm::ivec2         source_origin,
    glm::ivec2         source_size,
    const Render_pass& destination_renderpass,
    glm::ivec2         destination_origin
)
{
    m_impl->blit_framebuffer(source_renderpass, source_origin, source_size, destination_renderpass, destination_origin);
}
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
    m_impl->copy_from_texture(source_texture, source_slice,source_level, source_origin, source_size, destination_texture, destination_slice, destination_level, destination_origin);
}
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
    m_impl->copy_from_buffer(
        source_buffer,
        source_offset,
        source_bytes_per_row,
        source_bytes_per_image,
        source_size,
        destination_texture,
        destination_slice,
        destination_level,
        destination_origin
    );
}
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
    m_impl->copy_from_texture(
        source_texture,
        source_slice,
        source_level,
        source_origin,
        source_size,
        destination_buffer,
        destination_offset,
        destination_bytes_per_row,
        destination_bytes_per_image
    );
}
void Blit_command_encoder::generate_mipmaps(const Texture* texture)
{
    m_impl->generate_mipmaps(texture);
}
void Blit_command_encoder::fill_buffer(
    const Buffer*  buffer,
    std::uintptr_t offset,
    std::uintptr_t length,
    uint8_t        value
)
{
    m_impl->fill_buffer(
        buffer,
        offset,
        length,
        value
    );
}
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
    m_impl->copy_from_texture(
        source_texture,
        source_slice,
        source_level,
        destination_texture,
        destination_slice,
        destination_level,
        slice_count,
        level_count
    );
}
void Blit_command_encoder::copy_from_texture(const Texture* source_texture, const Texture* destination_texture)
{
    m_impl->copy_from_texture(source_texture, destination_texture);
}
void Blit_command_encoder::copy_from_buffer(
    const Buffer*  source_buffer,
    std::uintptr_t source_offset,
    const Buffer*  destination_buffer,
    std::uintptr_t destination_offset,
    std::uintptr_t size
)
{
    m_impl->copy_from_buffer(
        source_buffer,
        source_offset,
        destination_buffer,
        destination_offset,
        size
    );
}

} // namespace erhe::graphics
