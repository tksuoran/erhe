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

// Staging budget: the largest amount of pixel data held in host-visible
// staging at once. Loads larger than this flush (submit + fence wait +
// reclaim) mid-way instead of growing staging with the scene size.
constexpr std::size_t s_staging_byte_count = 64 * 1024 * 1024;

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

Image_transfer::Image_transfer(erhe::graphics::Device& graphics_device, const Image_transfer_mode mode)
    : m_graphics_device{graphics_device}
    , m_mode           {mode}
{
    if (m_mode == Image_transfer_mode::blocking_drain) {
        // The private ring exists only for the blocking mode. In
        // frame_recording mode staging comes from the device's shared ring
        // and is reclaimed by frame completion, so allocating 64 MiB per
        // loader here would be pure waste - and N concurrent loads would
        // multiply it.
        m_staging.emplace(
            graphics_device,
            erhe::graphics::Ring_buffer_create_info{
                .size              = s_staging_byte_count,
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
            &source_buffer,
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

    // Images larger than the whole staging ring take a dedicated one-shot
    // staging buffer instead (rare: e.g. 8k uncompressed sources). The
    // copies are flushed immediately so the buffer can be released right
    // away instead of piling up until some later frame completes.
    if (byte_count + s_staging_alignment > m_staging->get_capacity_byte_count()) {
        log_gltf->info("Image_transfer: {} byte image exceeds the staging ring; using a dedicated staging buffer", byte_count);
        erhe::graphics::Buffer staging_buffer{
            m_graphics_device,
            erhe::graphics::Buffer_create_info{
                .capacity_byte_count               = byte_count,
                .usage                             = erhe::graphics::Buffer_usage::transfer_src,
                .required_memory_property_bit_mask = erhe::graphics::Memory_property_flag_bit_mask::host_write,
                .init_data                         = pixels.data(),
                .debug_label                       = "Image_transfer oversize staging"
            }
        };
        record_copies(get_transfer_command_buffer(), image_info, staging_buffer, 0, texture, generate_mipmap);
        flush(); // GPU is done with staging_buffer when this returns
        return;
    }

    erhe::graphics::Ring_buffer_range range = m_staging->acquire(
        s_staging_alignment, erhe::graphics::Ring_buffer_usage::CPU_write, byte_count
    );
    if (range.get_span().empty()) {
        // Staging ring full of previously staged uploads: flush (submit +
        // wait + reclaim) and retry. This is what keeps staging bounded and
        // the load progressing without depending on frame completions.
        flush();
        range = m_staging->acquire(s_staging_alignment, erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
    }
    ERHE_VERIFY(!range.get_span().empty());

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

    // Staging comes from the device ring, so the range is reclaimed by frame
    // completion - which is exactly what makes this mode non-blocking, and
    // exactly why it requires a frame loop that keeps advancing. Note this
    // call never fails: it spills a new ring buffer sized to the request
    // when nothing has room, so remaining_budget_bytes above is what keeps
    // staging memory bounded.
    erhe::graphics::Ring_buffer_range range = m_graphics_device.allocate_ring_buffer_entry(
        erhe::graphics::Buffer_target::transfer_src,
        erhe::graphics::Ring_buffer_usage::CPU_write,
        byte_count
    );
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
