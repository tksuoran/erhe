#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/texture.hpp"

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace erhe::graphics::test {

// Blit_command_encoder::generate_mipmaps. Build a mip-mapped RGBA8 texture
// (8x8, 4 levels) and upload a half-black / half-white split to level 0 (the
// left 4 columns are {0,0,0,255}, the right 4 columns are {255,255,255,255}).
// generate_mipmaps blits level i-1 -> i with VK_FILTER_LINEAR and leaves every
// level in shader_read_only_optimal. Read a coarser level back and assert it is
// the linear downsample of level 0:
//   * level 1 (4x4) keeps the vertical black|white split (left cols ~0, right
//     cols ~255) -- a horizontal 2:1 box reduction of solid columns stays solid;
//   * level 3 (1x1) is the average of an equal black/white split, i.e. mid-gray
//     (~127), proving the chain actually averaged texels rather than copying.
TEST_F(Gpu_test, generate_mipmaps_downsamples_to_midgray)
{
    constexpr int width       = 8;
    constexpr int height      = 8;
    constexpr int level_count = 4; // 8 -> 4 -> 2 -> 1

    erhe::graphics::Device& graphics_device = device();

    // Level-0 pattern: left half black, right half white.
    std::vector<uint8_t> level0(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            const uint8_t     v = (x < (width / 2)) ? uint8_t{0} : uint8_t{255};
            level0[i + 0] = v;
            level0[i + 1] = v;
            level0[i + 2] = v;
            level0[i + 3] = 255u;
        }
    }

    // Host-visible source buffer holding the level-0 pattern.
    const std::size_t level0_bytes = level0.size();
    const std::shared_ptr<erhe::graphics::Buffer> source_buffer =
        make_host_buffer(level0_bytes, erhe::graphics::Buffer_usage::transfer_src, "mipmap source");
    {
        const std::span<std::byte> mapped = source_buffer->map_bytes(0, level0_bytes);
        std::memcpy(mapped.data(), level0.data(), level0_bytes);
        source_buffer->unmap();
    }

    // Mip-mapped texture: sampled (post-copy SHADER_READ transition + generate_mipmaps
    // final layout) + transfer_dst (blit dst for the chain, + buffer upload) +
    // transfer_src (blit src for the chain, + level readback). level_count set
    // explicitly so the chain is deterministic regardless of use_mipmaps default.
    const std::shared_ptr<erhe::graphics::Texture> texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = graphics_device,
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::sampled      |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm,
            .width       = width,
            .height      = height,
            .level_count = level_count,
            .debug_label = erhe::utility::Debug_label{"mipmap texture"}
        }
    );
    ASSERT_EQ(texture->get_level_count(), level_count);

    // Upload level 0 (copy_from_buffer transitions the level-0 subresource from
    // UNDEFINED and tracks SHADER_READ_ONLY_OPTIMAL), then generate the chain.
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(
                erhe::graphics::Buffer_texel_location{.buffer = source_buffer.get(), .offset = 0, .bytes_per_row = static_cast<std::uintptr_t>(width) * 4u, .bytes_per_image = static_cast<std::uintptr_t>(level0_bytes)},
                glm::ivec3{width, height, 1},
                erhe::graphics::Texture_location{.texture = texture.get(), .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}}
            );
            blit.generate_mipmaps(texture.get());
        }
    );

    // Level 1 (4x4) should keep the left|right black/white split: a horizontal
    // 2:1 reduction of solid columns is still solid.
    {
        const int                    level1_width  = texture->get_width(1);
        const int                    level1_height = texture->get_height(1);
        ASSERT_EQ(level1_width,  width  / 2);
        ASSERT_EQ(level1_height, height / 2);
        const std::vector<std::byte> raw = read_texture_level_bytes(*texture, 1, 4u);
        ASSERT_EQ(raw.size(), static_cast<std::size_t>(level1_width) * static_cast<std::size_t>(level1_height) * 4u);

        int dark_bad   = 0;
        int bright_bad = 0;
        for (int y = 0; y < level1_height; ++y) {
            for (int x = 0; x < level1_width; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(level1_width) + static_cast<std::size_t>(x)) * 4u;
                const int         r = static_cast<int>(std::to_integer<uint8_t>(raw[i + 0]));
                if (x < (level1_width / 2)) {
                    if (r > 8) { ++dark_bad; }
                } else {
                    if (r < 247) { ++bright_bad; }
                }
            }
        }
        EXPECT_EQ(dark_bad,   0) << dark_bad   << " level-1 left texels were not ~black";
        EXPECT_EQ(bright_bad, 0) << bright_bad << " level-1 right texels were not ~white";
    }

    // Level 3 (1x1) is the average of the equal black/white split: mid-gray.
    {
        ASSERT_EQ(texture->get_width(3),  1);
        ASSERT_EQ(texture->get_height(3), 1);
        const std::vector<std::byte> raw = read_texture_level_bytes(*texture, 3, 4u);
        ASSERT_EQ(raw.size(), 4u);
        const int r = static_cast<int>(std::to_integer<uint8_t>(raw[0]));
        const int g = static_cast<int>(std::to_integer<uint8_t>(raw[1]));
        const int b = static_cast<int>(std::to_integer<uint8_t>(raw[2]));
        // Linear average of 0 and 255 is 127.5; allow a generous tolerance for
        // the driver's rounding / filter weighting.
        EXPECT_GE(r, 110) << "level-3 red not mid-gray: " << r;
        EXPECT_LE(r, 145) << "level-3 red not mid-gray: " << r;
        EXPECT_GE(g, 110) << "level-3 green not mid-gray: " << g;
        EXPECT_LE(g, 145) << "level-3 green not mid-gray: " << g;
        EXPECT_GE(b, 110) << "level-3 blue not mid-gray: " << b;
        EXPECT_LE(b, 145) << "level-3 blue not mid-gray: " << b;
    }
}

// generate_mipmaps on a texture VIEW that starts at level 1 of an 8x8,
// 4-level image. The view's level 0 is the image's level 1 (4x4), so the
// blits must use 4x4 -> 2x2 -> 1x1 extents (a chain sized from the image's
// level 0 would blit an 8x8 source region out of a 4x4 level). Level 1 is
// seeded with a left-black / right-white split; the image's level 3 (1x1)
// then averages to mid-gray, and level 0 (never part of the view) keeps
// its seed.
TEST_F(Gpu_test, generate_mipmaps_on_level_view)
{
    constexpr int width       = 8;
    constexpr int height      = 8;
    constexpr int level_count = 4; // 8 -> 4 -> 2 -> 1

    erhe::graphics::Device& graphics_device = device();
    constexpr uint64_t usage_mask =
        erhe::graphics::Image_usage_flag_bit_mask::sampled      |
        erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
        erhe::graphics::Image_usage_flag_bit_mask::transfer_dst;
    const std::shared_ptr<erhe::graphics::Texture> texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = graphics_device,
            .usage_mask  = usage_mask,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_8_vec4_unorm,
            .width       = width,
            .height      = height,
            .level_count = level_count,
            .debug_label = erhe::utility::Debug_label{"mipmap view source"}
        }
    );

    const auto make_split = [](const int w, const int h, const uint8_t left, const uint8_t right) -> std::vector<uint8_t> {
        std::vector<uint8_t> texels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u;
                const uint8_t     v = (x < (w / 2)) ? left : right;
                texels[i + 0] = v;
                texels[i + 1] = v;
                texels[i + 2] = v;
                texels[i + 3] = 255u;
            }
        }
        return texels;
    };
    // Level 0: solid 77 (not part of the view; must survive). Level 1: black | white.
    const std::vector<uint8_t> level0_seed = make_split(width, height, 77u, 77u);
    const std::vector<uint8_t> level1_seed = make_split(width / 2, height / 2, 0u, 255u);
    seed_subresource_rgba8(*texture, 0, 0, level0_seed);
    seed_subresource_rgba8(*texture, 0, 1, level1_seed);

    erhe::graphics::Texture_create_info view_create_info = erhe::graphics::Texture_create_info::make_view(graphics_device, texture, 1, 0);
    view_create_info.usage_mask  = usage_mask;
    view_create_info.debug_label = erhe::utility::Debug_label{"mipmap view from level 1"};
    const std::shared_ptr<erhe::graphics::Texture> view = std::make_shared<erhe::graphics::Texture>(graphics_device, view_create_info);
    ASSERT_EQ(view->get_width(),       width / 2);
    ASSERT_EQ(view->get_height(),      height / 2);
    ASSERT_EQ(view->get_level_count(), level_count - 1);

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
            blit.generate_mipmaps(view.get());
        }
    );

    // Image level 3 (the view's last level) is the average of the split.
    {
        const std::vector<std::byte> raw = read_texture_level_bytes(*texture, 3, 4u);
        ASSERT_EQ(raw.size(), 4u);
        const int r = static_cast<int>(std::to_integer<uint8_t>(raw[0]));
        EXPECT_GE(r, 110) << "level-3 red not mid-gray: " << r;
        EXPECT_LE(r, 145) << "level-3 red not mid-gray: " << r;
    }
    // Image level 0 was outside the view and keeps its seed.
    {
        const std::vector<uint8_t> level0 = read_subresource_rgba8(*texture, 0, 0);
        ASSERT_EQ(level0.size(), level0_seed.size());
        EXPECT_EQ(level0, level0_seed) << "level 0 was modified by generating mipmaps of the level-1 view";
    }
}

} // namespace erhe::graphics::test
