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

namespace {

// Build a deterministic per-texel RGBA8 pattern for a width x height image,
// tightly packed at width*4 bytes per row.
auto make_rgba8_pattern(const int width, const int height) -> std::vector<uint8_t>
{
    std::vector<uint8_t> pattern(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            pattern[i + 0] = static_cast<uint8_t>((x * 23 + 5)       & 0xff);
            pattern[i + 1] = static_cast<uint8_t>((y * 41 + 11)      & 0xff);
            pattern[i + 2] = static_cast<uint8_t>(((x * y) * 7 + 2)  & 0xff);
            pattern[i + 3] = 255u;
        }
    }
    return pattern;
}

// A sampled | transfer_src | transfer_dst RGBA8 texture, built directly via
// Texture_create_info (no color_attachment usage, not a render target). sampled
// is required by the Vulkan blit encoder's post-copy SHADER_READ_ONLY transition
// (which both copy_from_buffer and the texture->texture copy_from_texture leave
// the image in and track); transfer_dst to receive a copy; transfer_src so the
// readback blit can copy it out.
auto make_sampled_copy_texture(
    erhe::graphics::Device& graphics_device,
    const int               width,
    const int               height,
    const char*             debug_label
) -> std::shared_ptr<erhe::graphics::Texture>
{
    return std::make_shared<erhe::graphics::Texture>(
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
            .debug_label = erhe::utility::Debug_label{debug_label}
        }
    );
}

} // namespace

// Blit_command_encoder::copy_from_texture (texture -> texture, whole-texture
// overload). Upload a known RGBA8 pattern into a source texture (via the
// buffer->texture copy_from_buffer, which leaves the source tracked in
// SHADER_READ_ONLY_OPTIMAL), copy the whole source image into a destination
// texture, transition the destination to transfer_src_optimal, and read it
// back. Asserts the destination bytes match the source pattern exactly.
//
// This exercises the engine fix that makes the texture->texture copy read the
// source's tracked layout for the pre-barrier (and restore it afterwards)
// instead of hardcoding SHADER_READ_ONLY_OPTIMAL, and that updates the
// destination's tracked layout to SHADER_READ_ONLY_OPTIMAL after the copy.
TEST_F(Gpu_test, copy_from_texture_whole_image)
{
    constexpr int width  = 8;
    constexpr int height = 8;

    const std::vector<uint8_t> source = make_rgba8_pattern(width, height);
    const std::size_t          bytes  = source.size();

    erhe::graphics::Device& graphics_device = device();

    // Host-visible staging buffer holding the source pattern.
    const std::shared_ptr<erhe::graphics::Buffer> staging =
        make_host_buffer(bytes, erhe::graphics::Buffer_usage::transfer_src, "copy_from_texture staging");
    {
        const std::span<std::byte> mapped = staging->map_bytes(0, bytes);
        std::memcpy(mapped.data(), source.data(), bytes);
        staging->unmap();
    }

    const std::shared_ptr<erhe::graphics::Texture> source_texture =
        make_sampled_copy_texture(graphics_device, width, height, "copy_from_texture source");
    const std::shared_ptr<erhe::graphics::Texture> destination_texture =
        make_sampled_copy_texture(graphics_device, width, height, "copy_from_texture destination");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);

            // Fill the source texture from the staging buffer. This leaves the
            // source texture tracked + physically in SHADER_READ_ONLY_OPTIMAL.
            blit.copy_from_buffer(
                staging.get(),
                0,                                        // source_offset
                static_cast<std::uintptr_t>(width) * 4u, // source_bytes_per_row
                static_cast<std::uintptr_t>(bytes),       // source_bytes_per_image
                glm::ivec3{width, height, 1},             // source_size
                source_texture.get(),
                0,                                        // destination_slice
                0,                                        // destination_level
                glm::ivec3{0, 0, 0}                       // destination_origin
            );

            // Texture -> texture copy of the whole image. The source enters in
            // SHADER_READ_ONLY_OPTIMAL (its tracked layout) and is restored to
            // it; the destination is left tracked in SHADER_READ_ONLY_OPTIMAL.
            blit.copy_from_texture(source_texture.get(), destination_texture.get());

            // read_texture_rgba8 needs the destination in transfer_src_optimal.
            command_buffer.transition_texture_layout(*destination_texture, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*destination_texture);
    ASSERT_EQ(pixels.size(), source.size());

    int mismatches = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (pixels[i] != source[i]) {
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " of " << source.size() << " bytes differed after texture->texture copy + readback";
}

// Blit_command_encoder::copy_from_texture (texture -> texture, explicit-origins
// overload), sub-rect copy. Copy a w x h sub-rectangle from a non-zero source
// origin into a non-zero destination origin, then read the destination back and
// assert the copied region landed at the destination origin with the source
// sub-rect's texels. Only the copied region is asserted: the pre-barrier
// transitions the destination from UNDEFINED, so texels outside the copied
// rectangle are not guaranteed to retain any prior contents.
TEST_F(Gpu_test, copy_from_texture_sub_rect)
{
    constexpr int width  = 8;
    constexpr int height = 8;

    // Sub-rect: a 3x2 block taken from source (2,1), placed at destination (4,5).
    constexpr int rect_w = 3;
    constexpr int rect_h = 2;
    constexpr int src_x  = 2;
    constexpr int src_y  = 1;
    constexpr int dst_x  = 4;
    constexpr int dst_y  = 5;

    const std::vector<uint8_t> source = make_rgba8_pattern(width, height);
    const std::size_t          bytes  = source.size();

    erhe::graphics::Device& graphics_device = device();

    const std::shared_ptr<erhe::graphics::Buffer> staging =
        make_host_buffer(bytes, erhe::graphics::Buffer_usage::transfer_src, "copy_from_texture sub staging");
    {
        const std::span<std::byte> mapped = staging->map_bytes(0, bytes);
        std::memcpy(mapped.data(), source.data(), bytes);
        staging->unmap();
    }

    const std::shared_ptr<erhe::graphics::Texture> source_texture =
        make_sampled_copy_texture(graphics_device, width, height, "copy_from_texture sub source");
    const std::shared_ptr<erhe::graphics::Texture> destination_texture =
        make_sampled_copy_texture(graphics_device, width, height, "copy_from_texture sub destination");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);

            blit.copy_from_buffer(
                staging.get(),
                0,
                static_cast<std::uintptr_t>(width) * 4u,
                static_cast<std::uintptr_t>(bytes),
                glm::ivec3{width, height, 1},
                source_texture.get(),
                0,
                0,
                glm::ivec3{0, 0, 0}
            );

            // Sub-rect texture -> texture copy with non-zero origins.
            blit.copy_from_texture(
                source_texture.get(),
                0,                                  // source_slice
                0,                                  // source_level
                glm::ivec3{src_x, src_y, 0},        // source_origin
                glm::ivec3{rect_w, rect_h, 1},      // source_size
                destination_texture.get(),
                0,                                  // destination_slice
                0,                                  // destination_level
                glm::ivec3{dst_x, dst_y, 0}         // destination_origin
            );

            command_buffer.transition_texture_layout(*destination_texture, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*destination_texture);
    ASSERT_EQ(pixels.size(), source.size());

    // Assert the copied rectangle landed at (dst_x, dst_y) carrying the source
    // texels from (src_x, src_y).
    int mismatches = 0;
    for (int ry = 0; ry < rect_h; ++ry) {
        for (int rx = 0; rx < rect_w; ++rx) {
            const std::size_t src_index =
                ((static_cast<std::size_t>(src_y + ry) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(src_x + rx)) * 4u;
            const std::size_t dst_index =
                ((static_cast<std::size_t>(dst_y + ry) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(dst_x + rx)) * 4u;
            for (std::size_t c = 0; c < 4u; ++c) {
                if (pixels[dst_index + c] != source[src_index + c]) {
                    ++mismatches;
                }
            }
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " of " << (rect_w * rect_h * 4) << " sub-rect bytes differed after texture->texture sub-rect copy";
}

} // namespace erhe::graphics::test
