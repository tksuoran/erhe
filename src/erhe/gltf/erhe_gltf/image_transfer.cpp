#include "image_transfer.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::gltf {

Image_transfer::Image_transfer(erhe::graphics::Device& graphics_device)
    : m_graphics_device{graphics_device}
    , m_texture_upload_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::pixel,
        "Image_transfer::m_texture_upload_buffer"
    }
{
}

auto Image_transfer::acquire_range(const std::size_t byte_count) -> erhe::graphics::Ring_buffer_range
{
    return m_texture_upload_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
}

void Image_transfer::upload_to_texture(
    const erhe::graphics::Image_info&  image_info,
    erhe::graphics::Ring_buffer_range& buffer_range,
    erhe::graphics::Texture&           texture,
    bool                               generate_mipmap
)
{
    erhe::graphics::Blit_command_encoder encoder{m_graphics_device};

    const erhe::graphics::Buffer*  source_buffer          = buffer_range.get_buffer()->get_buffer();
    const std::uintptr_t           source_offset          = buffer_range.get_byte_start_offset_in_buffer();
    const std::uintptr_t           source_bytes_per_row   = image_info.row_stride;
    const std::uintptr_t           source_bytes_per_image = image_info.row_stride * image_info.height;
    const glm::ivec3               source_size            = glm::ivec3{image_info.width, image_info.height, image_info.depth};
    const erhe::graphics::Texture* destination_texture    = &texture;
    const std::uintptr_t           destination_slice      = 0;
    const std::uintptr_t           destination_level      = 0;
    const glm::ivec3               destination_origin     = glm::ivec3{0, 0, 0};

    encoder.copy_from_buffer(
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

    if (generate_mipmap) {
        encoder.generate_mipmaps(destination_texture);
    }
}

} // namespace erhe::gltf
