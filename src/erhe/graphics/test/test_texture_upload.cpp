#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

namespace erhe::graphics::test {

// Command_buffer::upload_to_texture: stage a known RGBA8 pattern into a
// texture from host memory, transition it to transfer_src, and read it back.
// The upload helper transitions the image to transfer_dst_optimal internally
// and leaves it there, so the test transitions it to transfer_src_optimal in
// the same frame before read_texture_rgba8 blits it out. Exercises the
// buffer->image staging copy path and per-texel correctness.
TEST_F(Gpu_test, texture_upload_roundtrip)
{
    constexpr int width  = 8;
    constexpr int height = 8;

    // Deterministic per-texel RGBA8 pattern.
    std::vector<uint8_t> source(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            source[i + 0] = static_cast<uint8_t>(x * 16);          // 0..112
            source[i + 1] = static_cast<uint8_t>(y * 16);          // 0..112
            source[i + 2] = static_cast<uint8_t>((x + y) * 8);     // 0..112
            source[i + 3] = 255u;
        }
    }

    const std::shared_ptr<erhe::graphics::Texture> texture =
        make_color_target(width, height, erhe::dataformat::Format::format_8_vec4_unorm, /*include_transfer_dst=*/true);

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.upload_to_texture(
                *texture,
                0,                                              // level
                0,                                              // x
                0,                                              // y
                width,
                height,
                erhe::dataformat::Format::format_8_vec4_unorm,
                source.data(),
                width * 4                                       // row_stride bytes
            );
            // upload_to_texture leaves the image in transfer_dst_optimal;
            // read_texture_rgba8 needs transfer_src_optimal.
            command_buffer.transition_texture_layout(*texture, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*texture);
    ASSERT_EQ(pixels.size(), source.size());

    int mismatches = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (pixels[i] != source[i]) {
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " of " << source.size() << " bytes differed after upload + readback";
}

// Command_buffer::clear_texture: clear the whole texture to a known color via
// vkCmdClearColorImage, transition to transfer_src, and verify every texel.
// clear_texture also leaves the image in transfer_dst_optimal.
TEST_F(Gpu_test, texture_clear_constant)
{
    constexpr int width  = 8;
    constexpr int height = 8;

    // {32, 64, 96, 255} as unorm doubles.
    const std::array<double, 4> clear_value{ 32.0 / 255.0, 64.0 / 255.0, 96.0 / 255.0, 1.0 };

    const std::shared_ptr<erhe::graphics::Texture> texture =
        make_color_target(width, height, erhe::dataformat::Format::format_8_vec4_unorm, /*include_transfer_dst=*/true);

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.clear_texture(*texture, clear_value);
            command_buffer.transition_texture_layout(*texture, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*texture);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    int bad = 0;
    for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
        const bool ok =
            (std::abs(static_cast<int>(pixels[i + 0]) - 32) <= 1) &&
            (std::abs(static_cast<int>(pixels[i + 1]) - 64) <= 1) &&
            (std::abs(static_cast<int>(pixels[i + 2]) - 96) <= 1) &&
            (std::abs(static_cast<int>(pixels[i + 3]) - 255) <= 1);
        if (!ok) {
            ++bad;
        }
    }
    EXPECT_EQ(bad, 0) << bad << " texels did not match the clear color {32,64,96,255}";
}

} // namespace erhe::graphics::test
