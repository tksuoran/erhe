#include "erhe_graphics/image_encoder.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"

#include <astcenc.h>
#include <encoder/basisu_bc7e_scalar.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>

namespace erhe::graphics {

namespace {

[[nodiscard]] auto srgb_to_linear_table() -> const std::array<float, 256>&
{
    static const std::array<float, 256> table = []() {
        std::array<float, 256> result{};
        for (std::size_t i = 0; i < 256; ++i) {
            const float c = static_cast<float>(i) / 255.0f;
            result[i] = (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return result;
    }();
    return table;
}

constexpr std::size_t linear_to_srgb_table_size = 4096;

[[nodiscard]] auto linear_to_srgb_table() -> const std::array<std::uint8_t, linear_to_srgb_table_size>&
{
    static const std::array<std::uint8_t, linear_to_srgb_table_size> table = []() {
        std::array<std::uint8_t, linear_to_srgb_table_size> result{};
        for (std::size_t i = 0; i < linear_to_srgb_table_size; ++i) {
            const float l = static_cast<float>(i) / static_cast<float>(linear_to_srgb_table_size - 1);
            const float c = (l <= 0.0031308f) ? (l * 12.92f) : ((1.055f * std::pow(l, 1.0f / 2.4f)) - 0.055f);
            result[i] = static_cast<std::uint8_t>(std::clamp(std::lround(c * 255.0f), 0L, 255L));
        }
        return result;
    }();
    return table;
}

// One 2x2 box-filter halving; odd edges clamp.
void halve_rgba8(
    const std::vector<std::uint8_t>& source,
    const int                        width,
    const int                        height,
    const bool                       srgb,
    std::vector<std::uint8_t>&       destination
)
{
    const int dst_width  = std::max(1, width  / 2);
    const int dst_height = std::max(1, height / 2);
    destination.resize(static_cast<std::size_t>(dst_width) * static_cast<std::size_t>(dst_height) * 4);
    const std::array<float, 256>&                            to_linear = srgb_to_linear_table();
    const std::array<std::uint8_t, linear_to_srgb_table_size>& to_srgb   = linear_to_srgb_table();
    for (int y = 0; y < dst_height; ++y) {
        const int y0 = std::min(2 * y,       height - 1);
        const int y1 = std::min((2 * y) + 1, height - 1);
        for (int x = 0; x < dst_width; ++x) {
            const int x0 = std::min(2 * x,       width - 1);
            const int x1 = std::min((2 * x) + 1, width - 1);
            const std::uint8_t* p[4] = {
                &source[((static_cast<std::size_t>(y0) * width) + x0) * 4],
                &source[((static_cast<std::size_t>(y0) * width) + x1) * 4],
                &source[((static_cast<std::size_t>(y1) * width) + x0) * 4],
                &source[((static_cast<std::size_t>(y1) * width) + x1) * 4]
            };
            std::uint8_t* out = &destination[((static_cast<std::size_t>(y) * dst_width) + x) * 4];
            for (int c = 0; c < 4; ++c) {
                if (srgb && (c < 3)) {
                    const float sum = to_linear[p[0][c]] + to_linear[p[1][c]] + to_linear[p[2][c]] + to_linear[p[3][c]];
                    const std::size_t index = static_cast<std::size_t>(std::lround((sum * 0.25f) * static_cast<float>(linear_to_srgb_table_size - 1)));
                    out[c] = to_srgb[std::min(index, linear_to_srgb_table_size - 1)];
                } else {
                    const unsigned int sum = static_cast<unsigned int>(p[0][c]) + p[1][c] + p[2][c] + p[3][c];
                    out[c] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
        }
    }
}

void encode_bc7_level(
    const std::vector<std::uint8_t>& pixels,
    const int                        width,
    const int                        height,
    const bool                       srgb,
    std::vector<std::uint8_t>&       out
)
{
    static std::once_flag s_init;
    std::call_once(s_init, []() { bc7e_scalar::bc7e_compress_block_init(); });
    bc7e_scalar::bc7e_compress_block_params params{};
    bc7e_scalar::bc7e_compress_block_params_init_ultrafast(&params, srgb);

    const int block_columns = (width  + 3) / 4;
    const int block_rows    = (height + 3) / 4;
    std::vector<std::uint32_t> block_texels(static_cast<std::size_t>(block_columns) * 16);
    std::vector<std::uint64_t> blocks      (static_cast<std::size_t>(block_columns) * 2);
    for (int block_row = 0; block_row < block_rows; ++block_row) {
        for (int block_column = 0; block_column < block_columns; ++block_column) {
            for (int texel = 0; texel < 16; ++texel) {
                const int x = std::min((block_column * 4) + (texel % 4), width  - 1);
                const int y = std::min((block_row    * 4) + (texel / 4), height - 1);
                std::memcpy(
                    &block_texels[(static_cast<std::size_t>(block_column) * 16) + texel],
                    &pixels[((static_cast<std::size_t>(y) * width) + x) * 4],
                    4
                );
            }
        }
        bc7e_scalar::bc7e_compress_blocks(static_cast<std::uint32_t>(block_columns), blocks.data(), block_texels.data(), &params);
        const std::uint8_t* block_bytes = reinterpret_cast<const std::uint8_t*>(blocks.data());
        out.insert(out.end(), block_bytes, block_bytes + (blocks.size() * sizeof(std::uint64_t)));
    }
}

class Astc_encoder
{
public:
    explicit Astc_encoder(const bool srgb)
    {
        astcenc_config config{};
        astcenc_error error = astcenc_config_init(srgb ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR, 4, 4, 1, ASTCENC_PRE_FASTEST, 0, &config);
        if (error == ASTCENC_SUCCESS) {
            error = astcenc_context_alloc(&config, 1, &m_context);
        }
        if (error != ASTCENC_SUCCESS) {
            log_texture->warn("astcenc encoder setup failed: {}", astcenc_get_error_string(error));
            m_context = nullptr;
        }
    }
    ~Astc_encoder() noexcept
    {
        if (m_context != nullptr) {
            astcenc_context_free(m_context);
        }
    }
    Astc_encoder(const Astc_encoder&) = delete;
    Astc_encoder& operator=(const Astc_encoder&) = delete;

    [[nodiscard]] auto encode_level(std::vector<std::uint8_t>& pixels, const int width, const int height, std::vector<std::uint8_t>& out) -> bool
    {
        if (m_context == nullptr) {
            return false;
        }
        const std::size_t block_count = static_cast<std::size_t>((width + 3) / 4) * static_cast<std::size_t>((height + 3) / 4);
        const std::size_t offset      = out.size();
        out.resize(offset + (block_count * 16));
        void* slices[1] = { pixels.data() };
        astcenc_image image{
            .dim_x     = static_cast<unsigned int>(width),
            .dim_y     = static_cast<unsigned int>(height),
            .dim_z     = 1,
            .data_type = ASTCENC_TYPE_U8,
            .data      = slices
        };
        const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
        const astcenc_error error = astcenc_compress_image(m_context, &image, &swizzle, out.data() + offset, block_count * 16, 0);
        static_cast<void>(astcenc_compress_reset(m_context));
        if (error != ASTCENC_SUCCESS) {
            log_texture->warn("astcenc_compress_image() failed: {}", astcenc_get_error_string(error));
            return false;
        }
        return true;
    }

private:
    astcenc_context* m_context{nullptr};
};

[[nodiscard]] auto get_encoded_format(const Block_encode_format encode_format, const bool srgb) -> erhe::dataformat::Format
{
    switch (encode_format) {
        case Block_encode_format::bc7:      return srgb ? erhe::dataformat::Format::format_bc7_srgb      : erhe::dataformat::Format::format_bc7_unorm;
        case Block_encode_format::astc_4x4: return srgb ? erhe::dataformat::Format::format_astc_4x4_srgb : erhe::dataformat::Format::format_astc_4x4_unorm;
        default:                            return srgb ? erhe::dataformat::Format::format_8_vec4_srgb   : erhe::dataformat::Format::format_8_vec4_unorm;
    }
}

[[nodiscard]] auto is_rgba8(const erhe::dataformat::Format format) -> bool
{
    return (format == erhe::dataformat::Format::format_8_vec4_srgb) || (format == erhe::dataformat::Format::format_8_vec4_unorm);
}

} // anonymous namespace

auto is_identity(const Image_reduction& reduction) -> bool
{
    return (reduction.encode_format == Block_encode_format::none) && (reduction.downscale_levels == 0);
}

auto get_reduced_byte_count(const Image_info& source_info, const Image_reduction& reduction) -> std::size_t
{
    const int  width  = std::max(1, source_info.width  >> reduction.downscale_levels);
    const int  height = std::max(1, source_info.height >> reduction.downscale_levels);
    const bool srgb   = erhe::dataformat::is_srgb(source_info.format);
    const erhe::dataformat::Format format = (reduction.encode_format == Block_encode_format::none)
        ? source_info.format
        : get_encoded_format(reduction.encode_format, srgb);
    return erhe::dataformat::get_mip_chain_byte_count(
        format,
        static_cast<std::size_t>(width),
        static_cast<std::size_t>(height),
        static_cast<std::size_t>(get_texture_level_count(width, height))
    );
}

auto reduce_rgba8_image(
    const Image_info&             source_info,
    std::span<const std::uint8_t> source_pixels,
    const Image_reduction&        reduction,
    Image_info&                   out_info,
    std::vector<std::uint8_t>&    out_pixels
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!is_rgba8(source_info.format) || (source_info.level_count > 1) || (source_info.depth > 1)) {
        return false;
    }
    const bool srgb = (source_info.format == erhe::dataformat::Format::format_8_vec4_srgb);

    int width  = source_info.width;
    int height = source_info.height;
    std::vector<std::uint8_t> level{source_pixels.begin(), source_pixels.end()};
    std::vector<std::uint8_t> next;
    for (int i = 0; i < reduction.downscale_levels; ++i) {
        halve_rgba8(level, width, height, srgb, next);
        width  = std::max(1, width  / 2);
        height = std::max(1, height / 2);
        std::swap(level, next);
    }

    out_info = source_info;
    out_info.width  = width;
    out_info.height = height;
    out_info.depth  = 1;
    out_pixels.clear();

    if (reduction.encode_format == Block_encode_format::none) {
        out_info.level_count = 1;
        out_info.row_stride  = width * 4;
        out_pixels           = std::move(level);
        return true;
    }

    const int level_count = get_texture_level_count(width, height);
    out_info.level_count = level_count;
    out_info.row_stride  = 0;
    out_info.format      = get_encoded_format(reduction.encode_format, srgb);
    out_pixels.reserve(get_reduced_byte_count(source_info, reduction));

    std::optional<Astc_encoder> astc_encoder;
    if (reduction.encode_format == Block_encode_format::astc_4x4) {
        astc_encoder.emplace(srgb);
    }
    int level_width  = width;
    int level_height = height;
    for (int level_index = 0; level_index < level_count; ++level_index) {
        if (level_index > 0) {
            halve_rgba8(level, level_width, level_height, srgb, next);
            level_width  = std::max(1, level_width  / 2);
            level_height = std::max(1, level_height / 2);
            std::swap(level, next);
        }
        if (reduction.encode_format == Block_encode_format::bc7) {
            encode_bc7_level(level, level_width, level_height, srgb, out_pixels);
        } else if (!astc_encoder->encode_level(level, level_width, level_height, out_pixels)) {
            return false;
        }
    }
    return true;
}

} // namespace erhe::graphics
