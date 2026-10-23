#include "erhe_graphics/blit_command_encoder.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_blit_command_encoder.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_blit_command_encoder.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_METAL4)
# include "erhe_graphics/metal4/metal4_blit_command_encoder.hpp"
#elif defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_blit_command_encoder.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_NONE)
# include "erhe_graphics/null/null_blit_command_encoder.hpp"
#endif


namespace erhe::graphics {

auto clip_blit_framebuffer_region(
    const glm::ivec2 source_origin,
    const glm::ivec2 source_size,
    const glm::ivec2 source_extent,
    const glm::ivec2 destination_origin,
    const glm::ivec2 destination_extent
) -> Blit_framebuffer_region
{
    // The copy as a rectangle of offsets d in [0, source_size): clip d to
    // where both source_origin + d and destination_origin + d are inside.
    const glm::ivec2 d_begin = glm::max(glm::ivec2{0, 0}, glm::max(-source_origin, -destination_origin));
    const glm::ivec2 d_end   = glm::min(source_size, glm::min(source_extent - source_origin, destination_extent - destination_origin));
    if ((d_end.x <= d_begin.x) || (d_end.y <= d_begin.y)) {
        return Blit_framebuffer_region{};
    }
    return Blit_framebuffer_region{
        .source_origin      = source_origin + d_begin,
        .destination_origin = destination_origin + d_begin,
        .size               = d_end - d_begin
    };
}

Blit_command_encoder::Blit_command_encoder(Device& device, Command_buffer& command_buffer)
    : m_impl{device, command_buffer}
{
    static_assert(sizeof(Blit_command_encoder_impl) <= 128);
    static_assert(alignof(Blit_command_encoder_impl) <= 16);
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
void Blit_command_encoder::copy_from_texture(const Texture_location& source, const glm::ivec3 source_size, const Texture_location& destination)
{
    m_impl->copy_from_texture(source, source_size, destination);
}
void Blit_command_encoder::copy_from_buffer(const Buffer_texel_location& source, const glm::ivec3 source_size, const Texture_location& destination)
{
    m_impl->copy_from_buffer(source, source_size, destination);
}
void Blit_command_encoder::copy_from_texture(const Texture_location& source, const glm::ivec3 source_size, const Buffer_texel_location& destination)
{
    m_impl->copy_from_texture(source, source_size, destination);
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
