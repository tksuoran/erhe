#include "erhe_graphics/image_loader_ktx2.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/ktx2_container.hpp"
#include "erhe_profile/profile.hpp"

#include <transcoder/basisu_transcoder.h>
#include <astcenc.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string_view>
#include <vector>

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

// Content heuristic for X+Y normal maps without metadata: transcodes a small
// mip level to RGBA32 and accepts when the RGB slice is (near-)grayscale for
// nearly all pixels and the alpha slice is not all-opaque. A conventional RGB
// normal map is bluish (B near 1) so it can never look grayscale; a grayscale
// height / bump map has a constant all-255 padding alpha. The tolerance
// absorbs ETC1S / UASTC compression noise on an exactly grayscale source.
[[nodiscard]] auto sniff_two_component_normal(basist::ktx2_transcoder& transcoder) -> bool
{
    const std::uint32_t level_count = transcoder.get_levels();
    const std::uint32_t width       = transcoder.get_width();
    const std::uint32_t height      = transcoder.get_height();
    // Prefer the first level at most 64 texels wide/high (cheap, still a few
    // thousand samples); a file without such a mip is probed at full size.
    std::uint32_t probe_level = 0;
    for (std::uint32_t level = 0; level < level_count; ++level) {
        probe_level = level;
        const std::uint32_t max_extent = std::max(width >> level, height >> level);
        if (max_extent <= 64u) {
            break;
        }
    }
    const std::uint32_t probe_width  = std::max(1u, width  >> probe_level);
    const std::uint32_t probe_height = std::max(1u, height >> probe_level);
    const std::size_t   pixel_count  = static_cast<std::size_t>(probe_width) * probe_height;
    std::vector<std::uint8_t> pixels(pixel_count * 4);
    const bool transcode_ok = transcoder.transcode_image_level(
        probe_level,
        0, // layer_index
        0, // face_index
        pixels.data(),
        static_cast<std::uint32_t>(pixel_count),
        basist::transcoder_texture_format::cTFRGBA32
    );
    if (!transcode_ok) {
        return false;
    }
    constexpr int         gray_tolerance    = 12;
    constexpr double      gray_min_fraction = 0.98;
    std::size_t           gray_count        = 0;
    std::uint64_t         alpha_sum         = 0;
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const std::uint8_t* px = &pixels[i * 4];
        const int r = px[0];
        const int g = px[1];
        const int b = px[2];
        const int deviation = std::max({std::abs(r - g), std::abs(r - b), std::abs(g - b)});
        if (deviation <= gray_tolerance) {
            ++gray_count;
        }
        alpha_sum += px[3];
    }
    const bool rgb_is_grayscale = static_cast<double>(gray_count) >= gray_min_fraction * static_cast<double>(pixel_count);
    const bool alpha_is_used    = (static_cast<double>(alpha_sum) / static_cast<double>(pixel_count)) < 250.0;
    return rgb_is_grayscale && alpha_is_used;
}

}

// Which half of Image_loader_ktx2 serves the open image.
enum class Ktx2_payload : unsigned int
{
    basis              = 0, // Basis Universal, transcoded by basist
    raw_blocks         = 1, // raw blocks passed through
    raw_blocks_decoded = 2  // raw ASTC blocks decoded to RGBA8 by astcenc
};

class Image_loader_ktx2_impl_state
{
public:
    basist::ktx2_transcoder              transcoder;
    std::vector<std::uint8_t>            owned_data;   // backing storage for the path-based open()
    Image_info                           info;
    basist::transcoder_texture_format    transcoder_format{basist::transcoder_texture_format::cTFRGBA32};
    Ktx2_payload                         payload{Ktx2_payload::basis};
    Ktx2_container                       raw_container;  // raw block payloads only
    std::span<const std::uint8_t>        raw_file;       // the buffer passed to open(); alive until load()
    erhe::dataformat::Format             raw_block_format{erhe::dataformat::Format::format_undefined};
    bool                                 is_open{false};
};

namespace {

// Opens a raw block KTX2 file (non-zero vkFormat). Fills the state's
// raw_* members and info.
[[nodiscard]] auto open_raw_blocks(
    Image_loader_ktx2_impl_state&       state,
    const std::span<const std::uint8_t> buffer_view,
    const Astc_support                  astc_support
) -> bool
{
    Ktx2_container& container = state.raw_container;
    if (!parse_ktx2(buffer_view, container)) {
        return false;
    }
    const erhe::dataformat::Format block_format = get_ktx2_raw_block_format(container.vk_format);
    if (block_format == erhe::dataformat::Format::format_undefined) {
        log_texture->warn("KTX2: unsupported vkFormat {}", container.vk_format);
        return false;
    }
    if (
        (container.supercompression_scheme != static_cast<std::uint32_t>(Ktx2_supercompression::none)) &&
        (container.supercompression_scheme != static_cast<std::uint32_t>(Ktx2_supercompression::zstd))
    ) {
        log_texture->warn("KTX2: raw block image with supercompression scheme {} is not supported", container.supercompression_scheme);
        return false;
    }
    if ((container.depth > 1) || (container.layer_count > 1) || (container.face_count != 1) || (container.width == 0) || (container.height == 0)) {
        log_texture->warn(
            "KTX2: raw block image must be a single 2D image (depth {}, layers {}, faces {})",
            container.depth, container.layer_count, container.face_count
        );
        return false;
    }
    for (std::size_t level = 0, end = container.levels.size(); level < end; ++level) {
        const std::size_t level_width  = std::max<std::size_t>(1, container.width  >> level);
        const std::size_t level_height = std::max<std::size_t>(1, container.height >> level);
        const std::size_t expected     = erhe::dataformat::get_image_level_size_bytes(block_format, level_width, level_height);
        if (container.levels[level].uncompressed_byte_length != expected) {
            log_texture->warn("KTX2: level {} has {} bytes, expected {}", level, container.levels[level].uncompressed_byte_length, expected);
            return false;
        }
    }
    const bool srgb = (block_format == erhe::dataformat::Format::format_astc_4x4_srgb);
    state.raw_file         = buffer_view;
    state.raw_block_format = block_format;
    if (astc_support == Astc_support::native) {
        state.payload = Ktx2_payload::raw_blocks;
        state.info = Image_info{
            .width       = static_cast<int>(container.width),
            .height      = static_cast<int>(container.height),
            .depth       = 1,
            .level_count = static_cast<int>(container.levels.size()),
            .row_stride  = 0, // block-compressed data is tightly packed
            .format      = block_format,
            .two_component_normal = is_ktx2_two_component_normal(container),
            .decode_path = Image_decode_path::raw_blocks
        };
    } else {
        state.payload = Ktx2_payload::raw_blocks_decoded;
        state.info = Image_info{
            .width       = static_cast<int>(container.width),
            .height      = static_cast<int>(container.height),
            .depth       = 1,
            .level_count = 1,
            .row_stride  = static_cast<int>(container.width) * 4,
            .format      = srgb ? erhe::dataformat::Format::format_8_vec4_srgb : erhe::dataformat::Format::format_8_vec4_unorm,
            .two_component_normal = is_ktx2_two_component_normal(container),
            .decode_path = Image_decode_path::raw_blocks_decoded
        };
    }
    return true;
}

// Decodes the ASTC blocks of one level to tightly packed RGBA8 (sRGB-encoded
// values for an SRGB format, like the hardware decode before conversion).
[[nodiscard]] auto decode_astc_to_rgba8(
    const std::span<const std::uint8_t> blocks,
    const erhe::dataformat::Format      block_format,
    const std::uint32_t                 width,
    const std::uint32_t                 height,
    const std::span<std::uint8_t>       rgba8
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    const unsigned int    block_extent = static_cast<unsigned int>(erhe::dataformat::get_block_extent(block_format));
    const astcenc_profile profile      = (block_format == erhe::dataformat::Format::format_astc_4x4_srgb)
        ? ASTCENC_PRF_LDR_SRGB
        : ASTCENC_PRF_LDR;
    astcenc_config config{};
    astcenc_error error = astcenc_config_init(profile, block_extent, block_extent, 1, ASTCENC_PRE_FASTEST, ASTCENC_FLG_DECOMPRESS_ONLY, &config);
    if (error != ASTCENC_SUCCESS) {
        log_texture->warn("KTX2: astcenc_config_init() failed: {}", astcenc_get_error_string(error));
        return false;
    }
    astcenc_context* context = nullptr;
    error = astcenc_context_alloc(&config, 1, &context);
    if (error != ASTCENC_SUCCESS) {
        log_texture->warn("KTX2: astcenc_context_alloc() failed: {}", astcenc_get_error_string(error));
        return false;
    }
    void* slices[1] = { rgba8.data() };
    astcenc_image image{
        .dim_x     = width,
        .dim_y     = height,
        .dim_z     = 1,
        .data_type = ASTCENC_TYPE_U8,
        .data      = slices
    };
    const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
    error = astcenc_decompress_image(context, blocks.data(), blocks.size(), &image, &swizzle, 0);
    astcenc_context_free(context);
    if (error != ASTCENC_SUCCESS) {
        log_texture->warn("KTX2: astcenc_decompress_image() failed: {}", astcenc_get_error_string(error));
        return false;
    }
    return true;
}

} // anonymous namespace

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

auto Image_loader_ktx2::open(const std::filesystem::path& path, Image_info& image_info, const bool linear, const Transcode_format_preference transcode_format_preference, const Astc_support astc_support) -> bool
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
    return open(buffer_view, image_info, linear, transcode_format_preference, astc_support);
}

auto Image_loader_ktx2::open(const std::span<const std::uint8_t>& buffer_view, Image_info& image_info, const bool linear, const Transcode_format_preference transcode_format_preference, const Astc_support astc_support) -> bool
{
    ERHE_PROFILE_FUNCTION();

    // Note: unlike the file-based open(), this open() does not close()
    // first, because the file-based open() forwards here after filling
    // owned_data - and close() would discard it. The transcoder init below
    // resets any previous state.
    m_state->is_open  = false;
    m_state->raw_file = {};

    // Raw block payloads (non-zero vkFormat) are not Basis Universal data:
    // basist rejects them; they are passed through or decoded.
    if (is_ktx2_raw_blocks(buffer_view)) {
        if (!open_raw_blocks(*m_state, buffer_view, astc_support)) {
            return false;
        }
        image_info       = m_state->info;
        m_state->is_open = true;
        return true;
    }
    m_state->payload = Ktx2_payload::basis;

    ensure_basisu_transcoder_init();
    basist::ktx2_transcoder& transcoder = m_state->transcoder;
    if (!transcoder.init(buffer_view.data(), static_cast<std::uint32_t>(buffer_view.size()))) {
        log_texture->warn("KTX2: container parse failed");
        return false;
    }
    if (!transcoder.start_transcoding()) {
        log_texture->warn("KTX2: start_transcoding() failed");
        return false;
    }

    // `ktx encode --normal-mode` (toktx: --normal_mode / --normal_map) stores
    // a normal map as a two component X+Y map (X in RGB, Y in A) and records
    // the encode options in the KTXwriterScParams metadata key. Surface that
    // through Image_info so the material system can select the shader path
    // that reconstructs Z.
    bool two_component_normal = false;
    const basisu::uint8_vec* sc_params = transcoder.find_key("KTXwriterScParams");
    if (sc_params != nullptr) {
        const std::string_view params{reinterpret_cast<const char*>(sc_params->data()), sc_params->size()};
        two_component_normal =
            (params.find("--normal-mode") != std::string_view::npos) ||
            (params.find("--normal_mode") != std::string_view::npos) ||
            (params.find("--normal_map")  != std::string_view::npos);
    }
    // Files written by the basisu CLI (`basisu -normal_map
    // -separate_rg_to_color_alpha`) carry no metadata recording the swizzle,
    // so fall back to sniffing the content: an X+Y map's color slice is X
    // replicated to RGB (exactly grayscale before compression) and its alpha
    // slice is Y (never all-opaque, which also rejects a height map's padding
    // alpha). Only linear images with an alpha slice qualify; the flag is
    // only ever consumed for material normal-slot textures.
    if (!two_component_normal && linear && (transcoder.get_has_alpha() != 0u)) {
        two_component_normal = sniff_two_component_normal(transcoder);
    }

    switch (transcode_format_preference) {
        case Transcode_format_preference::bc7: {
            // Block-compressed target: keep the file's full mip chain (mipmap
            // generation cannot write block-compressed levels). An X+Y normal
            // map goes to BC5 instead of BC7: two full BC4 planes (X in R,
            // Y in G) beat BC7's shared endpoints on decorrelated channels.
            m_state->transcoder_format = two_component_normal
                ? basist::transcoder_texture_format::cTFBC5
                : basist::transcoder_texture_format::cTFBC7_RGBA;
            m_state->info = Image_info{
                .width       = static_cast<int>(transcoder.get_width()),
                .height      = static_cast<int>(transcoder.get_height()),
                .depth       = 1,
                .level_count = static_cast<int>(transcoder.get_levels()),
                .row_stride  = 0, // block-compressed data is tightly packed
                .format      = two_component_normal
                    ? erhe::dataformat::Format::format_bc5_unorm // normal maps are always linear
                    : linear ? erhe::dataformat::Format::format_bc7_unorm : erhe::dataformat::Format::format_bc7_srgb
            };
            break;
        }
        case Transcode_format_preference::astc_4x4: {
            // An X+Y normal map stays ASTC RGBA: the transcoder emits L+A
            // endpoint blocks (X replicated to RGB, Y in A), so the shader's
            // .ga decode applies - no separate two-channel format needed.
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
    m_state->info.two_component_normal = two_component_normal;
    m_state->info.decode_path          = Image_decode_path::basis_transcoded;
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

    const Ktx2_container& raw_container = m_state->raw_container;
    if (m_state->payload == Ktx2_payload::raw_blocks) {
        // Stored levels (inflated when supercompressed); open() verified
        // their sizes.
        std::size_t write_offset = 0;
        for (int level = 0; level < info.level_count; ++level) {
            const std::size_t byte_count = static_cast<std::size_t>(raw_container.levels[static_cast<std::size_t>(level)].uncompressed_byte_length);
            if (!raw_container.read_level(m_state->raw_file, static_cast<std::uint32_t>(level), transfer_buffer.subspan(write_offset, byte_count))) {
                return false;
            }
            write_offset += byte_count;
        }
        return true;
    }
    if (m_state->payload == Ktx2_payload::raw_blocks_decoded) {
        std::span<const std::uint8_t> blocks = raw_container.get_level_data(m_state->raw_file, 0);
        std::vector<std::uint8_t> inflated;
        if (raw_container.supercompression_scheme != static_cast<std::uint32_t>(Ktx2_supercompression::none)) {
            inflated.resize(static_cast<std::size_t>(raw_container.levels[0].uncompressed_byte_length));
            if (!raw_container.read_level(m_state->raw_file, 0, std::span<std::uint8_t>{inflated.data(), inflated.size()})) {
                return false;
            }
            blocks = std::span<const std::uint8_t>{inflated.data(), inflated.size()};
        }
        return decode_astc_to_rgba8(
            blocks,
            m_state->raw_block_format,
            static_cast<std::uint32_t>(info.width),
            static_cast<std::uint32_t>(info.height),
            transfer_buffer.subspan(0, total_byte_count)
        );
    }

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
    m_state->is_open  = false;
    m_state->raw_file = {};
    m_state->owned_data.clear();
}

} // namespace erhe::graphics
