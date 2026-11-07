// Basis Universal encoder (erhe_graphics/basis_encoder.hpp): a generated PNG
// encoded to UASTC KTX2 decodes back close to its pixels, carries its mip
// chain, transcodes to raw BC blocks, and keeps the two component normal
// marker. Deviceless.

#include "erhe_graphics/basis_encoder.hpp"
#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/ktx2_container.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto crc32(const std::span<const std::uint8_t> bytes) -> std::uint32_t
{
    std::uint32_t crc = 0xffffffffu;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = ((crc & 1u) != 0u) ? ((crc >> 1) ^ 0xedb88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xffffffffu;
}

void append_u32_be(std::vector<std::uint8_t>& out, const std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void append_chunk(std::vector<std::uint8_t>& png, const char* const tag, const std::vector<std::uint8_t>& data)
{
    append_u32_be(png, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body{tag, tag + 4};
    body.insert(body.end(), data.begin(), data.end());
    png.insert(png.end(), body.begin(), body.end());
    append_u32_be(png, crc32(body));
}

// An 8-bit RGBA PNG whose IDAT is one stored (uncompressed) deflate block.
[[nodiscard]] auto make_png(const std::uint32_t width, const std::uint32_t height, const std::vector<std::uint8_t>& rgba) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> raw;
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0); // filter none
        const auto row = rgba.begin() + static_cast<std::ptrdiff_t>(y * width * 4);
        raw.insert(raw.end(), row, row + static_cast<std::ptrdiff_t>(width * 4));
    }
    std::vector<std::uint8_t> zlib{0x78, 0x01, 0x01};
    const std::uint16_t length = static_cast<std::uint16_t>(raw.size());
    zlib.push_back(static_cast<std::uint8_t>(length & 0xffu));
    zlib.push_back(static_cast<std::uint8_t>(length >> 8));
    zlib.push_back(static_cast<std::uint8_t>(~length & 0xffu));
    zlib.push_back(static_cast<std::uint8_t>((~length >> 8) & 0xffu));
    zlib.insert(zlib.end(), raw.begin(), raw.end());
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    append_u32_be(zlib, (b << 16) | a);

    std::vector<std::uint8_t> png{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<std::uint8_t> header;
    append_u32_be(header, width);
    append_u32_be(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA
    append_chunk(png, "IHDR", header);
    append_chunk(png, "IDAT", zlib);
    append_chunk(png, "IEND", {});
    return png;
}

// A smooth opaque gradient (UASTC reproduces it closely).
[[nodiscard]] auto make_gradient(const std::uint32_t width, const std::uint32_t height) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> rgba;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            rgba.push_back(static_cast<std::uint8_t>(40 + (x * 8)));
            rgba.push_back(static_cast<std::uint8_t>(60 + (y * 12)));
            rgba.push_back(static_cast<std::uint8_t>(200 - (x * 4)));
            rgba.push_back(255);
        }
    }
    return rgba;
}

} // anonymous namespace

#if defined(ERHE_BASIS_ENCODER)

TEST(Basis_encoder, png_encodes_to_uastc_ktx2_with_mips_and_decodes_close)
{
    ASSERT_TRUE(erhe::graphics::is_basis_encoder_available());
    EXPECT_FALSE(erhe::graphics::get_basis_encoder_version().empty());
    const std::uint32_t width  = 16;
    const std::uint32_t height = 8;
    const std::vector<std::uint8_t> pixels = make_gradient(width, height);
    const std::vector<std::uint8_t> png    = make_png(width, height, pixels);

    erhe::graphics::Texture_properties properties{};
    const std::vector<std::uint8_t> ktx2 = erhe::graphics::encode_image_to_basis_ktx2(png, properties, erhe::graphics::Basis_encode_settings{});
    ASSERT_FALSE(ktx2.empty());
    EXPECT_TRUE(erhe::graphics::is_ktx2_basis(ktx2));
    // Deterministic: the same input encodes to the same bytes.
    EXPECT_EQ(erhe::graphics::encode_image_to_basis_ktx2(png, properties, erhe::graphics::Basis_encode_settings{}), ktx2);

    // Decoded back to RGBA8 by the load path: level 0 within a few steps of the source.
    erhe::graphics::Image_loader loader;
    erhe::graphics::Image_info   info;
    ASSERT_TRUE(loader.open(ktx2, info, false));
    EXPECT_EQ(info.width, static_cast<int>(width));
    EXPECT_EQ(info.height, static_cast<int>(height));
    std::vector<std::uint8_t> decoded(static_cast<std::size_t>(info.row_stride) * height * 2);
    ASSERT_TRUE(loader.load(decoded));
    loader.close();
    int max_error = 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < (width * 4); ++x) {
            const int source = pixels[(y * width * 4) + x];
            const int result = decoded[(static_cast<std::size_t>(y) * static_cast<std::size_t>(info.row_stride)) + x];
            max_error = std::max(max_error, std::abs(source - result));
        }
    }
    EXPECT_LE(max_error, 8);

    // The Export transcode to raw BC7 keeps the mip chain (16x8 .. 1x1).
    const std::vector<std::uint8_t> bc = erhe::graphics::transcode_ktx2_basis_to_raw_blocks(
        ktx2, erhe::graphics::Ktx2_block_target::bc, erhe::graphics::Ktx2_color_space::srgb,
        erhe::graphics::Ktx2_supercompression::zstd, erhe::graphics::Ktx2_normal_layout::standard
    );
    ASSERT_FALSE(bc.empty());
    erhe::graphics::Ktx2_container container;
    ASSERT_TRUE(erhe::graphics::parse_ktx2(bc, container));
    EXPECT_EQ(container.vk_format, erhe::graphics::ktx2_vk_format_bc7_srgb);
    EXPECT_EQ(container.levels.size(), 5u);
}

TEST(Basis_encoder, properties_decide_levels_and_normal_layout)
{
    const std::vector<std::uint8_t> png = make_png(8, 8, make_gradient(8, 8));
    erhe::graphics::Texture_properties no_mips{};
    no_mips.generate_mipmaps = false;
    const std::vector<std::uint8_t> single = erhe::graphics::encode_image_to_basis_ktx2(png, no_mips, erhe::graphics::Basis_encode_settings{});
    ASSERT_FALSE(single.empty());
    erhe::graphics::Ktx2_container container;
    ASSERT_TRUE(erhe::graphics::parse_ktx2(single, container));
    EXPECT_EQ(container.levels.size(), 1u);

    erhe::graphics::Texture_properties normal{};
    normal.transfer_function = erhe::graphics::Texture_transfer_function::linear;
    normal.normal_encoding   = erhe::graphics::Texture_normal_encoding::two_component;
    const std::vector<std::uint8_t> xy = erhe::graphics::encode_image_to_basis_ktx2(png, normal, erhe::graphics::Basis_encode_settings{});
    ASSERT_FALSE(xy.empty());
    erhe::graphics::Ktx2_container xy_container;
    ASSERT_TRUE(erhe::graphics::parse_ktx2(xy, xy_container));
    EXPECT_TRUE(erhe::graphics::is_ktx2_two_component_normal(xy_container));
    const std::vector<std::uint8_t> bc5 = erhe::graphics::transcode_ktx2_basis_to_raw_blocks(
        xy, erhe::graphics::Ktx2_block_target::bc, erhe::graphics::Ktx2_color_space::linear,
        erhe::graphics::Ktx2_supercompression::none, erhe::graphics::Ktx2_normal_layout::two_component
    );
    erhe::graphics::Ktx2_container bc5_container;
    ASSERT_TRUE(erhe::graphics::parse_ktx2(bc5, bc5_container));
    EXPECT_EQ(bc5_container.vk_format, erhe::graphics::ktx2_vk_format_bc5_unorm);

    // A KTX2 input is not a PNG / JPEG.
    EXPECT_TRUE(erhe::graphics::encode_image_to_basis_ktx2(xy, normal, erhe::graphics::Basis_encode_settings{}).empty());
}

#else

TEST(Basis_encoder, absent_without_erhe_basis_encoder)
{
    EXPECT_FALSE(erhe::graphics::is_basis_encoder_available());
    EXPECT_TRUE(erhe::graphics::encode_image_to_basis_ktx2(make_png(4, 4, make_gradient(4, 4)), {}, {}).empty());
}

#endif
