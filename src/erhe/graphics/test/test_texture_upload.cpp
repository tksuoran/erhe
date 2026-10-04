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

// Command_buffer::clear_texture on a texture VIEW clears only the view's own
// subresources. A 2-layer array is seeded with distinct colors, a view of
// layer 1 is cleared: layer 1 takes the clear color and layer 0 keeps its
// seed. (Clearing the whole VkImage would also touch layer 0, which is not
// in TRANSFER_DST_OPTIMAL at that point - a validation error - and would
// destroy its contents.)
TEST_F(Gpu_test, texture_clear_layer_view_spares_other_layers)
{
    constexpr int width       = 4;
    constexpr int height      = 4;
    constexpr int layer_count = 2;

    erhe::graphics::Device& graphics_device = device();
    constexpr uint64_t usage_mask =
        erhe::graphics::Image_usage_flag_bit_mask::sampled      |
        erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
        erhe::graphics::Image_usage_flag_bit_mask::transfer_dst;
    const std::shared_ptr<erhe::graphics::Texture> texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device            = graphics_device,
            .usage_mask        = usage_mask,
            .type              = erhe::graphics::Texture_type::texture_2d_array,
            .pixelformat       = erhe::dataformat::Format::format_8_vec4_unorm,
            .width             = width,
            .height            = height,
            .array_layer_count = layer_count,
            .level_count       = 1,
            .debug_label       = erhe::utility::Debug_label{"clear view source"}
        }
    );

    const std::size_t    texel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<uint8_t> layer0_seed(texel_count * 4u);
    std::vector<uint8_t> layer1_seed(texel_count * 4u);
    for (std::size_t i = 0; i < texel_count; ++i) {
        layer0_seed[i * 4u + 0] = 10u; layer0_seed[i * 4u + 1] = 20u; layer0_seed[i * 4u + 2] = 30u; layer0_seed[i * 4u + 3] = 255u;
        layer1_seed[i * 4u + 0] = 40u; layer1_seed[i * 4u + 1] = 50u; layer1_seed[i * 4u + 2] = 60u; layer1_seed[i * 4u + 3] = 255u;
    }
    seed_subresource_rgba8(*texture, 0, 0, layer0_seed);
    seed_subresource_rgba8(*texture, 1, 0, layer1_seed);

    erhe::graphics::Texture_create_info view_create_info = erhe::graphics::Texture_create_info::make_view(graphics_device, texture, 0, 1);
    view_create_info.type        = erhe::graphics::Texture_type::texture_2d;
    view_create_info.usage_mask  = usage_mask;
    view_create_info.debug_label = erhe::utility::Debug_label{"clear view of layer 1"};
    const std::shared_ptr<erhe::graphics::Texture> view = std::make_shared<erhe::graphics::Texture>(graphics_device, view_create_info);
    ASSERT_EQ(view->get_array_layer_count(), 1);

    // {200, 100, 0, 255} as unorm doubles.
    const std::array<double, 4> clear_value{ 200.0 / 255.0, 100.0 / 255.0, 0.0, 1.0 };
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.clear_texture(*view, clear_value);
        }
    );

    const std::vector<uint8_t> layer0 = read_subresource_rgba8(*texture, 0, 0);
    const std::vector<uint8_t> layer1 = read_subresource_rgba8(*texture, 1, 0);
    ASSERT_EQ(layer0.size(), texel_count * 4u);
    ASSERT_EQ(layer1.size(), texel_count * 4u);
    int layer0_changed = 0;
    int layer1_bad     = 0;
    for (std::size_t i = 0; i < texel_count; ++i) {
        if ((layer0[i * 4u + 0] != 10u) || (layer0[i * 4u + 1] != 20u) || (layer0[i * 4u + 2] != 30u)) {
            ++layer0_changed;
        }
        const bool ok =
            (std::abs(static_cast<int>(layer1[i * 4u + 0]) - 200) <= 1) &&
            (std::abs(static_cast<int>(layer1[i * 4u + 1]) - 100) <= 1) &&
            (layer1[i * 4u + 2] <= 1u);
        if (!ok) {
            ++layer1_bad;
        }
    }
    EXPECT_EQ(layer0_changed, 0) << layer0_changed << " layer-0 texels were touched by clearing the layer-1 view";
    EXPECT_EQ(layer1_bad,     0) << layer1_bad     << " layer-1 texels did not take the clear color";
}

} // namespace erhe::graphics::test
