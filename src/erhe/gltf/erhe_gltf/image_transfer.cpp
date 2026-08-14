#include "image_transfer.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::gltf {

Image_transfer::Image_transfer(
    erhe::graphics::Device&         graphics_device,
    erhe::graphics::Command_buffer& init_command_buffer
)
    : m_graphics_device{graphics_device}
    , m_texture_upload_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::transfer_src,
        "Image_transfer::m_texture_upload_buffer"
    }
    , m_init_command_buffer{init_command_buffer}
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
    erhe::graphics::Blit_command_encoder encoder{m_graphics_device, m_init_command_buffer};

    const erhe::graphics::Buffer*  source_buffer       = buffer_range.get_buffer()->get_buffer();
    const std::uintptr_t           source_offset       = buffer_range.get_byte_start_offset_in_buffer();
    const erhe::graphics::Texture* destination_texture = &texture;
    const std::uintptr_t           destination_slice   = 0;
    const glm::ivec3               destination_origin  = glm::ivec3{0, 0, 0};
    const erhe::dataformat::Format format              = image_info.format;
    const bool                     is_compressed       = erhe::dataformat::is_block_compressed(format);

    // The buffer range holds level_count levels contiguously, largest-first,
    // tightly packed (a single level for decoders that do not expose mip
    // chains). Copy each level to its own texture mip.
    const int      level_count  = std::max(1, image_info.level_count);
    std::uintptr_t level_offset = 0;
    for (int level = 0; level < level_count; ++level) {
        const int level_width  = std::max(1, image_info.width  >> level);
        const int level_height = std::max(1, image_info.height >> level);
        const std::uintptr_t level_byte_count = erhe::dataformat::get_image_level_size_bytes(
            format,
            static_cast<std::size_t>(level_width),
            static_cast<std::size_t>(level_height)
        );
        const std::uintptr_t bytes_per_row = is_compressed
            ? 0 // block-compressed uploads require tightly packed data; the backends ignore row pitch
            : static_cast<std::uintptr_t>(level_width) * erhe::dataformat::get_format_size_bytes(format);
        const glm::ivec3 source_size = glm::ivec3{level_width, level_height, image_info.depth};

        encoder.copy_from_buffer(
            source_buffer,
            source_offset + level_offset,
            bytes_per_row,
            level_byte_count,
            source_size,
            destination_texture,
            destination_slice,
            static_cast<std::uintptr_t>(level),
            destination_origin
        );
        level_offset += level_byte_count;
    }

    if (generate_mipmap) {
        encoder.generate_mipmaps(destination_texture);
    }
}

} // namespace erhe::gltf
