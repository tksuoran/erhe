#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace {

using erhe::graphics::test::Gpu_test;

// Banded uploads through a small ring (doc/plans/memory_budget.md step 1).
class Image_transfer_gpu_test : public Gpu_test
{
protected:
    static constexpr std::size_t small_ring_byte_count = 4096;

    [[nodiscard]] auto make_texture(const int width, const int height, const int level_count) -> std::shared_ptr<erhe::graphics::Texture>
    {
        return std::make_shared<erhe::graphics::Texture>(
            device(),
            erhe::graphics::Texture_create_info{
                .device      = device(),
                .usage_mask  =
                    erhe::graphics::Image_usage_flag_bit_mask::sampled |
                    erhe::graphics::Image_usage_flag_bit_mask::transfer_dst |
                    erhe::graphics::Image_usage_flag_bit_mask::transfer_src,
                .type        = erhe::graphics::Texture_type::texture_2d,
                .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm,
                .use_mipmaps = level_count > 1,
                .width       = width,
                .height      = height,
                .level_count = level_count,
                .debug_label = erhe::utility::Debug_label{"Image_transfer_gpu_test"}
            }
        );
    }

    // Image_transfer takes its command buffer from the open device frame.
    void upload_in_frame(const erhe::graphics::Image_info& image_info, const std::vector<std::uint8_t>& pixels, erhe::graphics::Texture& texture, const bool generate_mipmap)
    {
        ASSERT_TRUE(device().wait_frame());
        {
            erhe::gltf::Image_transfer image_transfer{device(), erhe::gltf::Image_transfer_mode::blocking_drain, small_ring_byte_count};
            image_transfer.upload(image_info, pixels, texture, generate_mipmap);
            image_transfer.flush();
        }
        ASSERT_TRUE(device().end_frame());
    }

    // Every texel of every level distinct: level, x and y in the channels.
    [[nodiscard]] static auto make_levels(const int width, const int height, const int level_count) -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> pixels;
        for (int level = 0; level < level_count; ++level) {
            const int level_width  = std::max(1, width  >> level);
            const int level_height = std::max(1, height >> level);
            for (int y = 0; y < level_height; ++y) {
                for (int x = 0; x < level_width; ++x) {
                    pixels.push_back(static_cast<std::uint8_t>(x));
                    pixels.push_back(static_cast<std::uint8_t>(y));
                    pixels.push_back(static_cast<std::uint8_t>((level * 37) + (x >> 8) + (y >> 8)));
                    pixels.push_back(static_cast<std::uint8_t>(x ^ y));
                }
            }
        }
        return pixels;
    }
};

TEST_F(Image_transfer_gpu_test, image_larger_than_ring_uploads_every_level_in_bands)
{
    constexpr int width       = 96;
    constexpr int height      = 70; // band rows do not divide it
    constexpr int level_count = 4;
    const std::vector<std::uint8_t> pixels = make_levels(width, height, level_count);
    ASSERT_GT(pixels.size(), small_ring_byte_count);

    std::shared_ptr<erhe::graphics::Texture> texture = make_texture(width, height, level_count);
    const erhe::graphics::Image_info image_info{
        .width       = width,
        .height      = height,
        .depth       = 1,
        .level_count = level_count,
        .row_stride  = width * 4,
        .format      = erhe::dataformat::Format::format_8_vec4_unorm
    };
    upload_in_frame(image_info, pixels, *texture, false);

    std::size_t level_offset = 0;
    for (int level = 0; level < level_count; ++level) {
        const std::size_t level_byte_count = static_cast<std::size_t>(std::max(1, width >> level)) * static_cast<std::size_t>(std::max(1, height >> level)) * 4;
        const std::vector<std::byte> read = read_texture_level_bytes(*texture, static_cast<unsigned int>(level), 4);
        ASSERT_EQ(read.size(), level_byte_count) << "level " << level;
        EXPECT_EQ(0, std::memcmp(read.data(), pixels.data() + level_offset, level_byte_count)) << "level " << level;
        level_offset += level_byte_count;
    }
}

TEST_F(Image_transfer_gpu_test, image_larger_than_ring_with_generated_mipmaps_keeps_level_zero)
{
    constexpr int width  = 64;
    constexpr int height = 64;
    const std::vector<std::uint8_t> pixels = make_levels(width, height, 1);
    ASSERT_GT(pixels.size(), small_ring_byte_count);

    std::shared_ptr<erhe::graphics::Texture> texture = make_texture(width, height, 7);
    const erhe::graphics::Image_info image_info{
        .width       = width,
        .height      = height,
        .depth       = 1,
        .level_count = 1,
        .row_stride  = width * 4,
        .format      = erhe::dataformat::Format::format_8_vec4_unorm
    };
    upload_in_frame(image_info, pixels, *texture, true);

    const std::vector<std::byte> level_0 = read_texture_level_bytes(*texture, 0, 4);
    ASSERT_EQ(level_0.size(), pixels.size());
    EXPECT_EQ(0, std::memcmp(level_0.data(), pixels.data(), pixels.size()));
    const std::vector<std::byte> last_level = read_texture_level_bytes(*texture, 6, 4);
    ASSERT_EQ(last_level.size(), std::size_t{4});
}

} // anonymous namespace
