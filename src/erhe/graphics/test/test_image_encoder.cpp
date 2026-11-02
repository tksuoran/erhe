#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/image_encoder.hpp"
#include "erhe_graphics/texture.hpp"

#include <astcenc.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

using erhe::dataformat::Format;
using erhe::graphics::Block_encode_format;
using erhe::graphics::Image_info;
using erhe::graphics::Image_reduction;

[[nodiscard]] auto make_info(const int width, const int height, const Format format) -> Image_info
{
    return Image_info{
        .width       = width,
        .height      = height,
        .depth       = 1,
        .level_count = 1,
        .row_stride  = width * 4,
        .format      = format
    };
}

// Smooth diagonal gradient, opaque.
[[nodiscard]] auto make_gradient(const int width, const int height) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> pixels;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            pixels.push_back(static_cast<std::uint8_t>((x * 255) / (width - 1)));
            pixels.push_back(static_cast<std::uint8_t>((y * 255) / (height - 1)));
            pixels.push_back(static_cast<std::uint8_t>(((x + y) * 255) / (width + height - 2)));
            pixels.push_back(255);
        }
    }
    return pixels;
}

TEST(Image_encoder, reduced_byte_count_is_the_mip_chain_of_the_result)
{
    const Image_info info = make_info(4096, 4096, Format::format_8_vec4_srgb);
    const int levels = erhe::graphics::get_texture_level_count(4096, 4096);
    EXPECT_EQ(
        erhe::graphics::get_reduced_byte_count(info, Image_reduction{}),
        erhe::dataformat::get_mip_chain_byte_count(Format::format_8_vec4_srgb, 4096, 4096, static_cast<std::size_t>(levels))
    );
    EXPECT_EQ(
        erhe::graphics::get_reduced_byte_count(info, Image_reduction{.encode_format = Block_encode_format::bc7, .downscale_levels = 1}),
        erhe::dataformat::get_mip_chain_byte_count(Format::format_bc7_srgb, 2048, 2048, static_cast<std::size_t>(levels - 1))
    );
}

TEST(Image_encoder, downscale_averages_unorm_linearly_and_srgb_in_linear_light)
{
    // 2x1 blocks of black and white: one halving gives one texel per pair.
    const std::vector<std::uint8_t> pixels{0, 0, 0, 0, 255, 255, 255, 255};
    for (const Format format : {Format::format_8_vec4_unorm, Format::format_8_vec4_srgb}) {
        Image_info                out_info{};
        std::vector<std::uint8_t> out_pixels;
        ASSERT_TRUE(erhe::graphics::reduce_rgba8_image(make_info(2, 1, format), pixels, Image_reduction{.downscale_levels = 1}, out_info, out_pixels));
        EXPECT_EQ(out_info.width, 1);
        EXPECT_EQ(out_info.height, 1);
        EXPECT_EQ(out_info.level_count, 1);
        ASSERT_EQ(out_pixels.size(), std::size_t{4});
        // unorm: (0 + 255) / 2; sRGB: linear 0.5 encodes to 188. Alpha is linear in both.
        EXPECT_EQ(out_pixels[0], (format == Format::format_8_vec4_unorm) ? 128 : 188);
        EXPECT_EQ(out_pixels[3], 128);
    }
}

TEST(Image_encoder, bc7_encodes_the_full_mip_chain)
{
    const std::vector<std::uint8_t> pixels = make_gradient(64, 48);
    Image_info                out_info{};
    std::vector<std::uint8_t> out_pixels;
    const Image_reduction reduction{.encode_format = Block_encode_format::bc7};
    ASSERT_TRUE(erhe::graphics::reduce_rgba8_image(make_info(64, 48, Format::format_8_vec4_srgb), pixels, reduction, out_info, out_pixels));
    EXPECT_EQ(out_info.format, Format::format_bc7_srgb);
    EXPECT_EQ(out_info.level_count, 7);
    EXPECT_EQ(out_pixels.size(), erhe::graphics::get_reduced_byte_count(make_info(64, 48, Format::format_8_vec4_srgb), reduction));

    std::vector<std::uint8_t> again;
    Image_info                again_info{};
    ASSERT_TRUE(erhe::graphics::reduce_rgba8_image(make_info(64, 48, Format::format_8_vec4_srgb), pixels, reduction, again_info, again));
    EXPECT_EQ(out_pixels, again);
}

TEST(Image_encoder, astc_level_zero_decodes_close_to_the_source)
{
    constexpr int width  = 64;
    constexpr int height = 64;
    const std::vector<std::uint8_t> pixels = make_gradient(width, height);
    Image_info                out_info{};
    std::vector<std::uint8_t> out_pixels;
    const Image_reduction reduction{.encode_format = Block_encode_format::astc_4x4};
    ASSERT_TRUE(erhe::graphics::reduce_rgba8_image(make_info(width, height, Format::format_8_vec4_unorm), pixels, reduction, out_info, out_pixels));
    EXPECT_EQ(out_info.format, Format::format_astc_4x4_unorm);
    EXPECT_EQ(out_info.level_count, 7);
    EXPECT_EQ(out_pixels.size(), erhe::graphics::get_reduced_byte_count(make_info(width, height, Format::format_8_vec4_unorm), reduction));

    astcenc_config config{};
    ASSERT_EQ(astcenc_config_init(ASTCENC_PRF_LDR, 4, 4, 1, ASTCENC_PRE_FASTEST, ASTCENC_FLG_DECOMPRESS_ONLY, &config), ASTCENC_SUCCESS);
    astcenc_context* context = nullptr;
    ASSERT_EQ(astcenc_context_alloc(&config, 1, &context), ASTCENC_SUCCESS);
    std::vector<std::uint8_t> decoded(static_cast<std::size_t>(width) * height * 4);
    void* slices[1] = { decoded.data() };
    astcenc_image image{.dim_x = width, .dim_y = height, .dim_z = 1, .data_type = ASTCENC_TYPE_U8, .data = slices};
    const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
    const std::size_t level_0_bytes = static_cast<std::size_t>(width / 4) * (height / 4) * 16;
    EXPECT_EQ(astcenc_decompress_image(context, out_pixels.data(), level_0_bytes, &image, &swizzle, 0), ASTCENC_SUCCESS);
    astcenc_context_free(context);

    int max_error = 0;
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        max_error = std::max(max_error, std::abs(static_cast<int>(pixels[i]) - static_cast<int>(decoded[i])));
    }
    EXPECT_LE(max_error, 8);
}

} // anonymous namespace

