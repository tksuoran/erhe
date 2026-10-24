#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
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

// A view of an array texture starting at layer 1 keeps the array type and
// sees every layer past its base (Texture_create_info::make_view). Each of
// the view's layers must read the source layer it maps to: view layer 0 is
// source layer 1, view layer 1 is source layer 2. (The OpenGL backend used to
// create every view with one layer, so view layer 1 did not exist.)
TEST_F(Gpu_test, texture_array_view_exposes_every_layer_past_its_base)
{
    constexpr int width       = 4;
    constexpr int height      = 4;
    constexpr int layer_count = 3;

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
            .debug_label       = erhe::utility::Debug_label{"array view source"}
        }
    );

    const std::size_t texel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::array<std::vector<uint8_t>, layer_count> seeds;
    for (int layer = 0; layer < layer_count; ++layer) {
        std::vector<uint8_t>& seed = seeds[static_cast<std::size_t>(layer)];
        seed.resize(texel_count * 4u);
        for (std::size_t i = 0; i < texel_count; ++i) {
            seed[i * 4u + 0] = static_cast<uint8_t>(40 * (layer + 1));
            seed[i * 4u + 1] = static_cast<uint8_t>(i);
            seed[i * 4u + 2] = static_cast<uint8_t>(200 - (40 * layer));
            seed[i * 4u + 3] = 255u;
        }
        seed_subresource_rgba8(*texture, static_cast<unsigned int>(layer), 0, seed);
    }

    erhe::graphics::Texture_create_info view_create_info = erhe::graphics::Texture_create_info::make_view(graphics_device, texture, 0, 1);
    view_create_info.usage_mask  = usage_mask;
    view_create_info.debug_label = erhe::utility::Debug_label{"view of layers 1..2"};
    const std::shared_ptr<erhe::graphics::Texture> view = std::make_shared<erhe::graphics::Texture>(graphics_device, view_create_info);
    ASSERT_EQ(view->get_texture_type(),      erhe::graphics::Texture_type::texture_2d_array);
    ASSERT_EQ(view->get_array_layer_count(), layer_count - 1);

    for (int view_layer = 0; view_layer < (layer_count - 1); ++view_layer) {
        const std::vector<uint8_t> texels = read_subresource_rgba8(*view, static_cast<unsigned int>(view_layer), 0);
        EXPECT_EQ(texels, seeds[static_cast<std::size_t>(view_layer + 1)])
            << "view layer " << view_layer << " does not read source layer " << (view_layer + 1);
    }
}

namespace {

// format_packed111110_vec3_unorm stores R11G11B10 unsigned floats in one
// 32-bit value, R in the low bits: an 11-bit float is a 5-bit exponent (bias
// 15) over a 6-bit mantissa, the 10-bit one a 5-bit exponent over 5 bits.
constexpr uint32_t c_packed_r_1_0  = 15u << 6u; // 1.0
constexpr uint32_t c_packed_g_0_5  = 14u << 6u; // 0.5
constexpr uint32_t c_packed_b_0_25 = 13u << 5u; // 0.25
constexpr uint32_t c_packed_texel  = c_packed_r_1_0 | (c_packed_g_0_5 << 11u) | (c_packed_b_0_25 << 22u);

constexpr uint64_t c_packed_usage_mask =
    erhe::graphics::Image_usage_flag_bit_mask::sampled      |
    erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
    erhe::graphics::Image_usage_flag_bit_mask::transfer_dst;

} // anonymous namespace

// Clearing an R11G11B10 float texture and copying it to a buffer yields 4
// bytes per texel holding the packed value. (The OpenGL backend used to
// transfer this format as three 32-bit floats, 12 bytes per texel, and
// aborted on the buffer size.)
TEST_F(Gpu_test, packed_r11g11b10_float_clear_reads_back_packed_texels)
{
    constexpr int                      width  = 4;
    constexpr int                      height = 4;
    constexpr erhe::dataformat::Format format = erhe::dataformat::Format::format_packed111110_vec3_unorm;
    erhe::graphics::Device& graphics_device = device();
    if (!graphics_device.probe_image_format_support(format, c_packed_usage_mask)) {
        GTEST_SKIP() << "format_packed111110_vec3_unorm is not supported for sampled + transfer images on this device";
    }
    ASSERT_EQ(erhe::dataformat::get_format_size_bytes(format), 4u);

    const std::shared_ptr<erhe::graphics::Texture> texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = graphics_device,
            .usage_mask  = c_packed_usage_mask,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = format,
            .width       = width,
            .height      = height,
            .debug_label = erhe::utility::Debug_label{"R11G11B10 clear target"}
        }
    );
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.clear_texture(*texture, std::array<double, 4>{1.0, 0.5, 0.25, 1.0});
        }
    );

    const std::vector<std::byte> raw = read_texture_level_bytes(*texture, 0, 4u);
    ASSERT_EQ(raw.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    int bad = 0;
    for (std::size_t i = 0; i < raw.size(); i += 4u) {
        uint32_t texel = 0;
        std::memcpy(&texel, raw.data() + i, sizeof(texel));
        if (texel != c_packed_texel) {
            ++bad;
        }
    }
    EXPECT_EQ(bad, 0) << bad << " texels differ from the packed (1.0, 0.5, 0.25) value";
}

// Packed R11G11B10 texels uploaded from a buffer read back unchanged: the
// upload and the readback both move 4 bytes per texel.
TEST_F(Gpu_test, packed_r11g11b10_float_upload_round_trips)
{
    constexpr int                      width  = 4;
    constexpr int                      height = 4;
    constexpr erhe::dataformat::Format format = erhe::dataformat::Format::format_packed111110_vec3_unorm;
    erhe::graphics::Device& graphics_device = device();
    if (!graphics_device.probe_image_format_support(format, c_packed_usage_mask)) {
        GTEST_SKIP() << "format_packed111110_vec3_unorm is not supported for sampled + transfer images on this device";
    }

    const std::shared_ptr<erhe::graphics::Texture> texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = graphics_device,
            .usage_mask  = c_packed_usage_mask,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = format,
            .width       = width,
            .height      = height,
            .debug_label = erhe::utility::Debug_label{"R11G11B10 upload target"}
        }
    );

    // Distinct texels: the R and B mantissas count the texel index.
    constexpr std::size_t texel_count   = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    constexpr std::size_t bytes_per_row = static_cast<std::size_t>(width) * 4u;
    constexpr std::size_t byte_count    = texel_count * 4u;
    std::vector<uint32_t> texels(texel_count);
    for (std::size_t i = 0; i < texel_count; ++i) {
        const uint32_t index = static_cast<uint32_t>(i);
        texels[i] = (c_packed_r_1_0 | index) | (c_packed_g_0_5 << 11u) | ((c_packed_b_0_25 | (index & 31u)) << 22u);
    }
    const std::shared_ptr<erhe::graphics::Buffer> source = make_host_buffer(byte_count, erhe::graphics::Buffer_usage::transfer_src, "R11G11B10 upload");
    {
        const std::span<std::byte> mapped = source->map_bytes(0, byte_count);
        std::memcpy(mapped.data(), texels.data(), byte_count);
        source->unmap();
    }
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(
                erhe::graphics::Buffer_texel_location{.buffer = source.get(), .offset = 0, .bytes_per_row = bytes_per_row, .bytes_per_image = byte_count},
                glm::ivec3{width, height, 1},
                erhe::graphics::Texture_location{.texture = texture.get(), .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}}
            );
        }
    );

    const std::vector<std::byte> raw = read_texture_level_bytes(*texture, 0, 4u);
    ASSERT_EQ(raw.size(), byte_count);
    std::vector<uint32_t> read_back(texel_count);
    std::memcpy(read_back.data(), raw.data(), byte_count);
    EXPECT_EQ(read_back, texels);
}

} // namespace erhe::graphics::test
