#include "image_transfer.hpp"

#include "gltf_log.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cstring>

namespace erhe::gltf {

namespace {

// vkCmdCopyBufferToImage requires bufferOffset to be a multiple of the
// texel block size; 16 covers every format the image loaders produce
// (RGBA8 = 4, fp16 RGBA = 8, BC / ASTC blocks = 8 or 16).
constexpr std::size_t s_staging_alignment = 16;

// Thread slot for the transfer command buffers. Texture uploads run on the
// thread that drives the glTF parse (per Gltf_parser: the GPU half is
// single-threaded), which is also the thread that owns this Image_transfer.
constexpr unsigned int s_transfer_thread_slot = 0;

} // anonymous namespace

Image_transfer::Image_transfer(erhe::graphics::Device& graphics_device)
    : Image_transfer{graphics_device, Image_transfer_mode::blocking_drain}
{
}

Image_transfer::Image_transfer(
    erhe::graphics::Device&   graphics_device,
    const Image_transfer_mode mode,
    const std::size_t         staging_capacity_byte_count
)
    : m_graphics_device            {graphics_device}
    , m_mode                       {mode}
    , m_staging_capacity_byte_count{staging_capacity_byte_count}
{
    ERHE_VERIFY(m_staging_capacity_byte_count > 0);
    if (m_mode == Image_transfer_mode::blocking_drain) {
        // The private ring exists only for the blocking mode. In
        // frame_recording mode staging comes from the device's capped
        // upload staging and is reclaimed by frame completion, so a private
        // ring per loader would be pure waste - and N concurrent loads would
        // multiply it. Loads larger than this ring flush (submit + fence
        // wait + reclaim) mid-way instead of growing staging with the scene.
        m_staging.emplace(
            graphics_device,
            erhe::graphics::Ring_buffer_create_info{
                .size              = m_staging_capacity_byte_count,
                .ring_buffer_usage = erhe::graphics::Ring_buffer_usage::CPU_write,
                .debug_label       = "Image_transfer staging"
            }
        );
    }
}

Image_transfer::~Image_transfer()
{
    flush();
}

auto Image_transfer::get_transfer_command_buffer() -> erhe::graphics::Command_buffer&
{
    if (m_command_buffer == nullptr) {
        m_command_buffer = &m_graphics_device.get_command_buffer(s_transfer_thread_slot);
        m_command_buffer->begin();
    }
    return *m_command_buffer;
}

void Image_transfer::flush()
{
    if (m_command_buffer == nullptr) {
        return; // also the frame_recording case: the frame owns the command buffer
    }
    // Uploads run outside the device frame (and possibly off the main
    // thread), so the driver-owned temporaries created here - the command
    // buffer, the blit encoders - need a pool of their own. See
    // Scoped_transient_object_pool.
    erhe::graphics::Scoped_transient_object_pool object_pool{};
    m_command_buffer->end();
    m_graphics_device.submit_command_buffer_and_wait(*m_command_buffer);
    m_command_buffer = nullptr;
    // Every consumer of the staged ranges was recorded in the command
    // buffer we just waited on, so all staging space is reclaimable.
    m_staging->complete_all_syncs();
}

void Image_transfer::record_copies(
    erhe::graphics::Command_buffer&   command_buffer,
    const erhe::graphics::Image_info& image_info,
    const erhe::graphics::Buffer&     source_buffer,
    const std::size_t                 source_offset,
    erhe::graphics::Texture&          texture,
    const bool                        generate_mipmap
)
{
    erhe::graphics::Blit_command_encoder encoder{m_graphics_device, command_buffer};

    const erhe::graphics::Texture* destination_texture = &texture;
    const std::uintptr_t           destination_slice   = 0;
    const glm::ivec3               destination_origin  = glm::ivec3{0, 0, 0};
    const erhe::dataformat::Format format              = image_info.format;
    const bool                     is_compressed       = erhe::dataformat::is_block_compressed(format);

    // The staged data holds level_count levels contiguously, largest-first,
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
            erhe::graphics::Buffer_texel_location{.buffer = &source_buffer, .offset = source_offset + level_offset, .bytes_per_row = bytes_per_row, .bytes_per_image = level_byte_count},
            source_size,
            erhe::graphics::Texture_location{.texture = destination_texture, .slice = destination_slice, .level = static_cast<std::uintptr_t>(level), .origin = destination_origin}
        );
        level_offset += level_byte_count;
    }

    if (generate_mipmap) {
        encoder.generate_mipmaps(destination_texture);
    }
}

void Image_transfer::upload(
    const erhe::graphics::Image_info&   image_info,
    const std::span<const std::uint8_t> pixels,
    erhe::graphics::Texture&            texture,
    const bool                          generate_mipmap
)
{
    ERHE_VERIFY(m_mode == Image_transfer_mode::blocking_drain);
    const std::size_t byte_count = pixels.size();
    ERHE_VERIFY(byte_count > 0);

    erhe::graphics::Scoped_transient_object_pool object_pool{};

    // Bands, not a one-shot buffer: doc/plans/memory_budget.md step 1.
    if (byte_count + s_staging_alignment > m_staging->get_capacity_byte_count()) {
        upload_in_bands(image_info, pixels, texture, generate_mipmap);
        return;
    }

    erhe::graphics::Ring_buffer_range range = acquire_staging(byte_count);

    std::memcpy(range.get_span().data(), pixels.data(), byte_count);
    range.bytes_written(byte_count);
    range.close();
    record_copies(
        get_transfer_command_buffer(),
        image_info,
        *m_staging->get_buffer(),
        range.get_byte_start_offset_in_buffer(),
        texture,
        generate_mipmap
    );
    range.release();
}

auto Image_transfer::acquire_staging(const std::size_t byte_count) -> erhe::graphics::Ring_buffer_range
{
    erhe::graphics::Ring_buffer_range range = m_staging->acquire(
        s_staging_alignment, erhe::graphics::Ring_buffer_usage::CPU_write, byte_count
    );
    if (range.get_span().empty()) {
        // Ring full: flush (submit + wait + reclaim) and retry.
        flush();
        range = m_staging->acquire(s_staging_alignment, erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
    }
    ERHE_VERIFY(!range.get_span().empty());
    return range;
}

void Image_transfer::upload_in_bands(
    const erhe::graphics::Image_info&   image_info,
    const std::span<const std::uint8_t> pixels,
    erhe::graphics::Texture&            texture,
    const bool                          generate_mipmap
)
{
    const erhe::dataformat::Format format       = image_info.format;
    const bool                     is_compressed = erhe::dataformat::is_block_compressed(format);
    const std::size_t              block_extent  = erhe::dataformat::get_block_extent(format);
    // Half the ring per band, so a band fits beside the one before it.
    const std::size_t              band_capacity = (m_staging->get_capacity_byte_count() / 2) - s_staging_alignment;

    const int   level_count  = std::max(1, image_info.level_count);
    std::size_t level_offset = 0;
    for (int level = 0; level < level_count; ++level) {
        const std::size_t level_width      = static_cast<std::size_t>(std::max(1, image_info.width  >> level));
        const std::size_t level_height     = static_cast<std::size_t>(std::max(1, image_info.height >> level));
        const std::size_t level_byte_count = erhe::dataformat::get_image_level_size_bytes(format, level_width, level_height);
        const std::size_t block_row_count  = (level_height + block_extent - 1) / block_extent;
        const std::size_t block_row_bytes  = level_byte_count / block_row_count;
        const std::size_t band_block_rows  = std::max<std::size_t>(1, band_capacity / block_row_bytes);
        ERHE_VERIFY(level_offset + level_byte_count <= pixels.size());

        for (std::size_t first_block_row = 0; first_block_row < block_row_count; first_block_row += band_block_rows) {
            const std::size_t band_rows       = std::min(band_block_rows, block_row_count - first_block_row);
            const std::size_t band_byte_count = band_rows * block_row_bytes;
            const std::size_t first_texel_row = first_block_row * block_extent;
            const std::size_t band_height     = std::min(band_rows * block_extent, level_height - first_texel_row);

            erhe::graphics::Ring_buffer_range range = acquire_staging(band_byte_count);
            std::memcpy(range.get_span().data(), pixels.data() + level_offset + (first_block_row * block_row_bytes), band_byte_count);
            range.bytes_written(band_byte_count);
            range.close();

            erhe::graphics::Blit_command_encoder encoder{m_graphics_device, get_transfer_command_buffer()};
            encoder.copy_from_buffer(
                erhe::graphics::Buffer_texel_location{
                    .buffer          = m_staging->get_buffer(),
                    .offset          = range.get_byte_start_offset_in_buffer(),
                    .bytes_per_row   = is_compressed ? 0 : block_row_bytes,
                    .bytes_per_image = band_byte_count
                },
                glm::ivec3{static_cast<int>(level_width), static_cast<int>(band_height), image_info.depth},
                erhe::graphics::Texture_location{
                    .texture = &texture,
                    .slice   = 0,
                    .level   = static_cast<std::uintptr_t>(level),
                    .origin  = glm::ivec3{0, static_cast<int>(first_texel_row), 0}
                }
            );
            range.release();
        }
        level_offset += level_byte_count;
    }

    if (generate_mipmap) {
        erhe::graphics::Blit_command_encoder encoder{m_graphics_device, get_transfer_command_buffer()};
        encoder.generate_mipmaps(&texture);
    }
}

auto Image_transfer::upload_into_frame(
    erhe::graphics::Command_buffer&     command_buffer,
    const erhe::graphics::Image_info&   image_info,
    const std::span<const std::uint8_t> pixels,
    erhe::graphics::Texture&            texture,
    const bool                          generate_mipmap,
    std::size_t&                        remaining_budget_bytes
) -> Image_upload_result
{
    ERHE_VERIFY(m_mode == Image_transfer_mode::frame_recording);
    const std::size_t byte_count = pixels.size();
    ERHE_VERIFY(byte_count > 0);

    if (remaining_budget_bytes == 0) {
        return Image_upload_result::budget_exhausted;
    }

    // Staging comes from the device's capped upload staging, so the range is
    // reclaimed by frame completion - which is exactly what makes this mode
    // non-blocking, and exactly why it requires a frame loop that keeps
    // advancing. No room (the cap is held by staging that frames in flight
    // still read) means wait: nothing is recorded and the caller retries
    // next frame. An image larger than the whole cap is served by a one-shot
    // buffer once nothing else is staged.
    erhe::graphics::Ring_buffer_range range = m_graphics_device.allocate_upload_staging(
        erhe::graphics::Buffer_target::transfer_src,
        byte_count,
        m_staging_capacity_byte_count
    );
    if (range.get_buffer() == nullptr) {
        return Image_upload_result::budget_exhausted;
    }
    ERHE_VERIFY(range.get_span().size() >= byte_count);

    std::memcpy(range.get_span().data(), pixels.data(), byte_count);
    range.bytes_written(byte_count);
    range.close();
    record_copies(
        command_buffer,
        image_info,
        *range.get_buffer()->get_buffer(),
        range.get_byte_start_offset_in_buffer(),
        texture,
        generate_mipmap
    );
    range.release();

    // An image bigger than what is left this frame still goes in one piece;
    // the budget bounds how much a frame starts, not how far one image may
    // overshoot.
    remaining_budget_bytes -= std::min(remaining_budget_bytes, byte_count);
    return Image_upload_result::recorded;
}

} // namespace erhe::gltf
