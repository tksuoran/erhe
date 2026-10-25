// Raw block KTX2 container (erhe_graphics/ktx2_container.hpp) and the raw
// ASTC path of Image_loader_ktx2: write / parse round trip of a generated
// ASTC 4x4 image (header, level index, DFD, key/value data), pass-through
// load with native ASTC support, and the astcenc CPU decode for a device
// without it. Deviceless.

#include "erhe_graphics/image_loader_ktx2.hpp"
#include "erhe_graphics/ktx2_container.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace {

using erhe::dataformat::Format;

// ASTC void-extent block: a constant LDR color, each channel UNORM16
// (ASTC specification, "Void-Extent Blocks"): block mode 0x1FC, LDR, the
// reserved bits and all four extent coordinates set to ones.
[[nodiscard]] auto make_constant_block(const std::uint16_t r, const std::uint16_t g, const std::uint16_t b, const std::uint16_t a) -> std::array<std::uint8_t, 16>
{
    std::array<std::uint8_t, 16> block{
        0xFCu, 0xFDu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u
    };
    const std::uint16_t channels[4] = {r, g, b, a};
    for (std::size_t c = 0; c < 4; ++c) {
        block[8 + (c * 2) + 0] = static_cast<std::uint8_t>(channels[c] & 0xFFu);
        block[8 + (c * 2) + 1] = static_cast<std::uint8_t>(channels[c] >> 8);
    }
    return block;
}

[[nodiscard]] auto read_u32(const std::span<const std::uint8_t> bytes, const std::size_t offset) -> std::uint32_t
{
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

enum class Normal_marker : unsigned int { none = 0, two_component = 1 };

// A 12 x 8 image (3 x 2 blocks) with its full chain: 12x8, 6x4, 3x2, 1x1.
class Test_image
{
public:
    Test_image()
    {
        const std::array<std::uint8_t, 16> block = make_constant_block(0xFFFFu, 0x8080u, 0x0000u, 0xFFFFu);
        const std::size_t block_counts[4] = {6, 2, 1, 1};
        for (std::size_t level = 0; level < 4; ++level) {
            for (std::size_t i = 0; i < block_counts[level]; ++i) {
                // Distinguish levels in the pass-through check: the last
                // byte of A (UNORM16 high byte) stays 0xFF, the low byte
                // carries the level.
                std::array<std::uint8_t, 16> level_block = block;
                level_block[14] = static_cast<std::uint8_t>(0xF0u + level);
                levels[level].insert(levels[level].end(), level_block.begin(), level_block.end());
            }
        }
    }

    [[nodiscard]] auto make_input(const Format format, const Normal_marker normal_marker) const -> erhe::graphics::Ktx2_raw_blocks_image
    {
        erhe::graphics::Ktx2_raw_blocks_image image{
            .format               = format,
            .width                = 12,
            .height               = 8,
            .levels               = {},
            .two_component_normal = (normal_marker == Normal_marker::two_component)
        };
        for (const std::vector<std::uint8_t>& level : levels) {
            image.levels.push_back(std::span<const std::uint8_t>{level.data(), level.size()});
        }
        return image;
    }

    std::array<std::vector<std::uint8_t>, 4> levels;
};

} // anonymous namespace

TEST(Ktx2Container, raw_astc_round_trip)
{
    const Test_image test_image;
    const std::vector<std::uint8_t> file = erhe::graphics::write_ktx2_raw_blocks(test_image.make_input(Format::format_astc_4x4_srgb, Normal_marker::none));
    ASSERT_FALSE(file.empty());
    const std::span<const std::uint8_t> file_view{file.data(), file.size()};
    EXPECT_TRUE (erhe::graphics::Image_loader_ktx2::is_ktx2(file_view));
    EXPECT_TRUE (erhe::graphics::is_ktx2_raw_blocks(file_view));
    EXPECT_FALSE(erhe::graphics::is_ktx2_basis(file_view));

    erhe::graphics::Ktx2_container container;
    ASSERT_TRUE(erhe::graphics::parse_ktx2(file_view, container));
    EXPECT_EQ(container.vk_format, erhe::graphics::ktx2_vk_format_astc_4x4_srgb);
    EXPECT_EQ(container.type_size, 1u);
    EXPECT_EQ(container.width, 12u);
    EXPECT_EQ(container.height, 8u);
    EXPECT_EQ(container.depth, 0u);
    EXPECT_EQ(container.layer_count, 0u);
    EXPECT_EQ(container.face_count, 1u);
    EXPECT_EQ(container.level_count, 4u);
    EXPECT_EQ(container.supercompression_scheme, 0u);
    ASSERT_EQ(container.levels.size(), 4u);
    for (std::uint32_t level = 0; level < 4; ++level) {
        const std::span<const std::uint8_t> data = container.get_level_data(file_view, level);
        ASSERT_EQ(data.size(), test_image.levels[level].size()) << "level " << level;
        EXPECT_EQ(std::memcmp(data.data(), test_image.levels[level].data(), data.size()), 0) << "level " << level;
        EXPECT_EQ(container.levels[level].byte_offset % 16u, 0u) << "level " << level;
    }
    // Levels are stored smallest first.
    EXPECT_GT(container.levels[0].byte_offset, container.levels[3].byte_offset);

    // Data format descriptor: one basic block, ASTC model, sRGB transfer,
    // 4x4x1 texel blocks of 16 bytes, one 128-bit ASTC data sample.
    ASSERT_EQ(container.dfd.size(), 44u);
    EXPECT_EQ(read_u32(container.dfd,  0), 44u);
    EXPECT_EQ(read_u32(container.dfd,  4), 0u);
    EXPECT_EQ(read_u32(container.dfd,  8), 2u | (40u << 16));
    EXPECT_EQ(read_u32(container.dfd, 12) & 0xFFu, 162u);          // KHR_DF_MODEL_ASTC
    EXPECT_EQ((read_u32(container.dfd, 12) >> 8) & 0xFFu, 1u);     // BT709 primaries
    EXPECT_EQ((read_u32(container.dfd, 12) >> 16) & 0xFFu, 2u);    // KHR_DF_TRANSFER_SRGB
    EXPECT_EQ(read_u32(container.dfd, 16), 0x0303u);               // 4x4x1x1 texel block
    EXPECT_EQ(read_u32(container.dfd, 20), 16u);                   // bytesPlane0
    EXPECT_EQ((read_u32(container.dfd, 28) >> 16) & 0xFFu, 127u);  // bitLength - 1
    EXPECT_EQ(read_u32(container.dfd, 40), 0xFFFFFFFFu);           // sampleUpper

    EXPECT_EQ(container.find_value("KTXwriter"), "erhe");
    EXPECT_FALSE(erhe::graphics::is_ktx2_two_component_normal(container));

    // UNORM and the normal map marker.
    const std::vector<std::uint8_t> normal_file = erhe::graphics::write_ktx2_raw_blocks(test_image.make_input(Format::format_astc_4x4_unorm, Normal_marker::two_component));
    erhe::graphics::Ktx2_container normal_container;
    ASSERT_TRUE(erhe::graphics::parse_ktx2(std::span<const std::uint8_t>{normal_file.data(), normal_file.size()}, normal_container));
    EXPECT_EQ(normal_container.vk_format, erhe::graphics::ktx2_vk_format_astc_4x4_unorm);
    EXPECT_EQ((read_u32(normal_container.dfd, 12) >> 16) & 0xFFu, 1u); // KHR_DF_TRANSFER_LINEAR
    EXPECT_TRUE(erhe::graphics::is_ktx2_two_component_normal(normal_container));
}

TEST(Ktx2Container, write_rejects_wrong_level_size)
{
    Test_image test_image;
    test_image.levels[1].resize(test_image.levels[1].size() - 16);
    EXPECT_TRUE(erhe::graphics::write_ktx2_raw_blocks(test_image.make_input(Format::format_astc_4x4_srgb, Normal_marker::none)).empty());
}

TEST(Ktx2Container, loader_passes_raw_astc_through_with_native_support)
{
    const Test_image test_image;
    const std::vector<std::uint8_t> file = erhe::graphics::write_ktx2_raw_blocks(test_image.make_input(Format::format_astc_4x4_unorm, Normal_marker::two_component));
    erhe::graphics::Image_loader_ktx2 loader;
    erhe::graphics::Image_info        info{};
    ASSERT_TRUE(loader.open(std::span<const std::uint8_t>{file.data(), file.size()}, info, false, erhe::graphics::Transcode_format_preference::bc7, erhe::graphics::Astc_support::native));
    EXPECT_EQ(info.format, Format::format_astc_4x4_unorm); // the file's vkFormat wins over 'linear'
    EXPECT_EQ(info.width, 12);
    EXPECT_EQ(info.height, 8);
    EXPECT_EQ(info.level_count, 4);
    EXPECT_TRUE(info.two_component_normal);
    EXPECT_EQ(info.decode_path, erhe::graphics::Image_decode_path::raw_blocks);

    std::vector<std::uint8_t> expected;
    for (const std::vector<std::uint8_t>& level : test_image.levels) {
        expected.insert(expected.end(), level.begin(), level.end());
    }
    ASSERT_EQ(erhe::dataformat::get_mip_chain_byte_count(info.format, 12, 8, 4), expected.size());
    std::vector<std::uint8_t> loaded(expected.size());
    ASSERT_TRUE(loader.load(std::span<std::uint8_t>{loaded.data(), loaded.size()}));
    EXPECT_EQ(loaded, expected);
}

TEST(Ktx2Container, loader_decodes_raw_astc_without_native_support)
{
    const Test_image test_image;
    for (const Format block_format : {Format::format_astc_4x4_srgb, Format::format_astc_4x4_unorm}) {
        const std::vector<std::uint8_t> file = erhe::graphics::write_ktx2_raw_blocks(test_image.make_input(block_format, Normal_marker::none));
        erhe::graphics::Image_loader_ktx2 loader;
        erhe::graphics::Image_info        info{};
        ASSERT_TRUE(loader.open(std::span<const std::uint8_t>{file.data(), file.size()}, info, true, erhe::graphics::Transcode_format_preference::bc7, erhe::graphics::Astc_support::decode_to_rgba8));
        const bool srgb = (block_format == Format::format_astc_4x4_srgb);
        EXPECT_EQ(info.format, srgb ? Format::format_8_vec4_srgb : Format::format_8_vec4_unorm);
        EXPECT_EQ(info.level_count, 1);
        EXPECT_EQ(info.row_stride, 12 * 4);
        EXPECT_EQ(info.decode_path, erhe::graphics::Image_decode_path::raw_blocks_decoded);

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(12 * 8 * 4), std::uint8_t{0x55});
        ASSERT_TRUE(loader.load(std::span<std::uint8_t>{pixels.data(), pixels.size()}));
        // The constant block: R 0xFFFF, G 0x8080, B 0, A 0xFFF0 (level 0)
        // decode to 8-bit (255, 128, 0, 255).
        for (std::size_t i = 0; i < 12 * 8; ++i) {
            ASSERT_EQ(pixels[(i * 4) + 0], 255u) << "pixel " << i;
            ASSERT_EQ(pixels[(i * 4) + 1], 128u) << "pixel " << i;
            ASSERT_EQ(pixels[(i * 4) + 2],   0u) << "pixel " << i;
            ASSERT_EQ(pixels[(i * 4) + 3], 255u) << "pixel " << i;
        }
    }
}

// Zstandard supercompression: each level compressed on its own, the index
// keeps the uncompressed sizes, bytesPlane0 is 0; both loader paths inflate.
TEST(Ktx2Container, zstd_supercompressed_round_trip)
{
    const Test_image test_image;
    erhe::graphics::Ktx2_raw_blocks_image input = test_image.make_input(Format::format_astc_4x4_srgb, Normal_marker::two_component);
    input.supercompression = erhe::graphics::Ktx2_supercompression::zstd;
    const std::vector<std::uint8_t> file = erhe::graphics::write_ktx2_raw_blocks(input);
    ASSERT_FALSE(file.empty());
    const std::span<const std::uint8_t> file_view{file.data(), file.size()};

    erhe::graphics::Ktx2_container container;
    ASSERT_TRUE(erhe::graphics::parse_ktx2(file_view, container));
    EXPECT_EQ(container.supercompression_scheme, 2u);
    EXPECT_EQ(read_u32(container.dfd, 20), 0u); // bytesPlane0
    EXPECT_TRUE(erhe::graphics::is_ktx2_two_component_normal(container));
    ASSERT_EQ(container.levels.size(), 4u);
    std::vector<std::uint8_t> expected;
    for (std::uint32_t level = 0; level < 4; ++level) {
        EXPECT_EQ(container.levels[level].uncompressed_byte_length, test_image.levels[level].size());
        std::vector<std::uint8_t> inflated(test_image.levels[level].size());
        ASSERT_TRUE(container.read_level(file_view, level, std::span<std::uint8_t>{inflated.data(), inflated.size()}));
        EXPECT_EQ(inflated, test_image.levels[level]) << "level " << level;
        expected.insert(expected.end(), test_image.levels[level].begin(), test_image.levels[level].end());
    }
    // Constant blocks compress.
    EXPECT_LT(container.levels[0].byte_length, container.levels[0].uncompressed_byte_length);

    {
        erhe::graphics::Image_loader_ktx2 loader;
        erhe::graphics::Image_info        info{};
        ASSERT_TRUE(loader.open(file_view, info, false, erhe::graphics::Transcode_format_preference::bc7, erhe::graphics::Astc_support::native));
        EXPECT_EQ(info.decode_path, erhe::graphics::Image_decode_path::raw_blocks);
        std::vector<std::uint8_t> loaded(expected.size());
        ASSERT_TRUE(loader.load(std::span<std::uint8_t>{loaded.data(), loaded.size()}));
        EXPECT_EQ(loaded, expected);
    }
    {
        erhe::graphics::Image_loader_ktx2 loader;
        erhe::graphics::Image_info        info{};
        ASSERT_TRUE(loader.open(file_view, info, false, erhe::graphics::Transcode_format_preference::bc7, erhe::graphics::Astc_support::decode_to_rgba8));
        EXPECT_EQ(info.decode_path, erhe::graphics::Image_decode_path::raw_blocks_decoded);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(12 * 8 * 4));
        ASSERT_TRUE(loader.load(std::span<std::uint8_t>{pixels.data(), pixels.size()}));
        EXPECT_EQ(pixels[0], 255u);
        EXPECT_EQ(pixels[1], 128u);
        EXPECT_EQ(pixels[2], 0u);
        EXPECT_EQ(pixels[3], 255u);
    }
}
