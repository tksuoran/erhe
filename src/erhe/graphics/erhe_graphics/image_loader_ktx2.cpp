#include "erhe_graphics/image_loader_ktx2.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_profile/profile.hpp"

#include <transcoder/basisu_transcoder.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>

namespace erhe::graphics {

namespace {

// The transcoder's global tables (ETC1S codebooks etc.) must be initialized
// once before any transcoder use.
std::once_flag s_basisu_transcoder_init_once{};

void ensure_basisu_transcoder_init()
{
    std::call_once(
        s_basisu_transcoder_init_once,
        []() {
            basist::basisu_transcoder_init();
        }
    );
}

constexpr std::uint8_t ktx2_identifier[12] = {
    0xABu, 0x4Bu, 0x54u, 0x58u, 0x20u, 0x32u, 0x30u, 0xBBu, 0x0Du, 0x0Au, 0x1Au, 0x0Au
};

}

class Image_loader_ktx2_impl_state
{
public:
    basist::ktx2_transcoder              transcoder;
    std::vector<std::uint8_t>            owned_data;   // backing storage for the path-based open()
    Image_info                           info;
    basist::transcoder_texture_format    transcoder_format{basist::transcoder_texture_format::cTFRGBA32};
    bool                                 is_open{false};
};

Image_loader_ktx2::Image_loader_ktx2()
    : m_state{std::make_unique<Image_loader_ktx2_impl_state>()}
{
}

Image_loader_ktx2::~Image_loader_ktx2() noexcept = default;

auto Image_loader_ktx2::is_ktx2(const std::span<const std::uint8_t>& buffer_view) -> bool
{
    if (buffer_view.size() < sizeof(ktx2_identifier)) {
        return false;
    }
    return std::memcmp(buffer_view.data(), ktx2_identifier, sizeof(ktx2_identifier)) == 0;
}

auto Image_loader_ktx2::open(const std::filesystem::path& path, Image_info& image_info, const bool linear, const Transcode_format_preference transcode_format_preference) -> bool
{
    ERHE_PROFILE_FUNCTION();

    close();

    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream) {
        log_texture->warn("KTX2: cannot open file '{}'", path.string());
        return false;
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0) {
        log_texture->warn("KTX2: empty file '{}'", path.string());
        return false;
    }
    stream.seekg(0, std::ios::beg);
    m_state->owned_data.resize(static_cast<std::size_t>(size));
    if (!stream.read(reinterpret_cast<char*>(m_state->owned_data.data()), size)) {
        log_texture->warn("KTX2: read failed for '{}'", path.string());
        m_state->owned_data.clear();
        return false;
    }
    const std::span<const std::uint8_t> buffer_view{m_state->owned_data.data(), m_state->owned_data.size()};
    return open(buffer_view, image_info, linear, transcode_format_preference);
}

auto Image_loader_ktx2::open(const std::span<const std::uint8_t>& buffer_view, Image_info& image_info, const bool linear, const Transcode_format_preference transcode_format_preference) -> bool
{
    ERHE_PROFILE_FUNCTION();

    // Note: unlike the file-based open(), this open() does not close()
    // first, because the file-based open() forwards here after filling
    // owned_data - and close() would discard it. The transcoder init below
    // resets any previous state.
    ensure_basisu_transcoder_init();

    m_state->is_open = false;
    basist::ktx2_transcoder& transcoder = m_state->transcoder;
    if (!transcoder.init(buffer_view.data(), static_cast<std::uint32_t>(buffer_view.size()))) {
        log_texture->warn("KTX2: container parse failed");
        return false;
    }
    if (!transcoder.start_transcoding()) {
        log_texture->warn("KTX2: start_transcoding() failed");
        return false;
    }

    switch (transcode_format_preference) {
        case Transcode_format_preference::bc7: {
            // Block-compressed target: keep the file's full mip chain (mipmap
            // generation cannot write block-compressed levels).
            m_state->transcoder_format = basist::transcoder_texture_format::cTFBC7_RGBA;
            m_state->info = Image_info{
                .width       = static_cast<int>(transcoder.get_width()),
                .height      = static_cast<int>(transcoder.get_height()),
                .depth       = 1,
                .level_count = static_cast<int>(transcoder.get_levels()),
                .row_stride  = 0, // block-compressed data is tightly packed
                .format      = linear ? erhe::dataformat::Format::format_bc7_unorm : erhe::dataformat::Format::format_bc7_srgb
            };
            break;
        }
        case Transcode_format_preference::astc_4x4: {
            m_state->transcoder_format = basist::transcoder_texture_format::cTFASTC_4x4_RGBA;
            m_state->info = Image_info{
                .width       = static_cast<int>(transcoder.get_width()),
                .height      = static_cast<int>(transcoder.get_height()),
                .depth       = 1,
                .level_count = static_cast<int>(transcoder.get_levels()),
                .row_stride  = 0, // block-compressed data is tightly packed
                .format      = linear ? erhe::dataformat::Format::format_astc_4x4_unorm : erhe::dataformat::Format::format_astc_4x4_srgb
            };
            break;
        }
        case Transcode_format_preference::rgba8:
        default: {
            // Only mip level 0 of the first layer / face is exposed; the GPU
            // mipmap generation path rebuilds the chain from it.
            m_state->transcoder_format = basist::transcoder_texture_format::cTFRGBA32;
            m_state->info = Image_info{
                .width       = static_cast<int>(transcoder.get_width()),
                .height      = static_cast<int>(transcoder.get_height()),
                .depth       = 1,
                .level_count = 1,
                .row_stride  = static_cast<int>(transcoder.get_width()) * 4,
                .format      = linear ? erhe::dataformat::Format::format_8_vec4_unorm : erhe::dataformat::Format::format_8_vec4_srgb
            };
            break;
        }
    }
    image_info       = m_state->info;
    m_state->is_open = true;
    return true;
}

auto Image_loader_ktx2::load(std::span<std::uint8_t> transfer_buffer) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!m_state->is_open) {
        return false;
    }
    const Image_info& info = m_state->info;
    const erhe::dataformat::Format format = info.format;
    const std::size_t total_byte_count = erhe::dataformat::get_mip_chain_byte_count(
        format,
        static_cast<std::size_t>(info.width),
        static_cast<std::size_t>(info.height),
        static_cast<std::size_t>(info.level_count)
    );
    if (transfer_buffer.size() < total_byte_count) {
        log_texture->warn("KTX2: transfer buffer too small: {} < {}", transfer_buffer.size(), total_byte_count);
        return false;
    }
    const bool is_compressed = erhe::dataformat::is_block_compressed(format);

    // All levels are written contiguously, largest-first, tightly packed -
    // the same layout Image_loader_dds produces and upload_to_texture expects.
    std::size_t write_offset = 0;
    for (int level = 0; level < info.level_count; ++level) {
        const std::size_t level_width  = std::max(std::size_t{1}, static_cast<std::size_t>(info.width)  >> level);
        const std::size_t level_height = std::max(std::size_t{1}, static_cast<std::size_t>(info.height) >> level);
        const std::size_t level_byte_count = erhe::dataformat::get_image_level_size_bytes(format, level_width, level_height);
        std::size_t block_or_pixel_count;
        if (is_compressed) {
            const std::size_t block_extent = erhe::dataformat::get_block_extent(format);
            const std::size_t block_count_x = (level_width  + block_extent - 1) / block_extent;
            const std::size_t block_count_y = (level_height + block_extent - 1) / block_extent;
            block_or_pixel_count = block_count_x * block_count_y;
        } else {
            block_or_pixel_count = level_width * level_height;
        }
        const bool transcode_ok = m_state->transcoder.transcode_image_level(
            static_cast<std::uint32_t>(level),
            0, // layer_index
            0, // face_index
            transfer_buffer.data() + write_offset,
            static_cast<std::uint32_t>(block_or_pixel_count),
            m_state->transcoder_format
        );
        if (!transcode_ok) {
            log_texture->warn("KTX2: transcode_image_level() failed for level {}", level);
            return false;
        }
        write_offset += level_byte_count;
    }
    return true;
}

void Image_loader_ktx2::close()
{
    m_state->is_open = false;
    m_state->owned_data.clear();
}

} // namespace erhe::graphics
