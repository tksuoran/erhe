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
            pattern[i + 0] = static_cast<uint8_t>((x * 17 + 3) & 0xff);
            pattern[i + 1] = static_cast<uint8_t>((y * 31 + 7) & 0xff);
            pattern[i + 2] = static_cast<uint8_t>(((x + y) * 13 + 1) & 0xff);
            pattern[i + 3] = 255u;
        }
    }
    return pattern;
}

} // namespace

// Blit_command_encoder::copy_from_buffer (buffer -> texture). Host-write a known
// RGBA8 pattern into a transfer_src buffer, copy it into a fresh texture's level
// 0, transition the texture to transfer_src_optimal, and read it back. Asserts
// the bytes match exactly.
//
// The destination texture is created with sampled usage (in addition to
// transfer_src | transfer_dst): the Vulkan blit encoder's buffer->image copy
// hardcodes a post-copy transition to SHADER_READ_ONLY_OPTIMAL and tracks that
// layout, which requires the image to have been created with SAMPLED usage. The
// texture is built directly via Texture_create_info (not via make_color_target)
// so it carries no color_attachment usage and is not a render target.
TEST_F(Gpu_test, copy_from_buffer_to_texture)
{
    constexpr int width  = 8;
    constexpr int height = 8;

    const std::vector<uint8_t> source = make_rgba8_pattern(width, height);
    const std::size_t          bytes  = source.size();

    erhe::graphics::Device& graphics_device = device();

    // Host-visible source buffer with the pattern.
    const std::shared_ptr<erhe::graphics::Buffer> source_buffer =
        make_host_buffer(bytes, erhe::graphics::Buffer_usage::transfer_src, "copy_from_buffer source");
    {
        const std::span<std::byte> mapped = source_buffer->map_bytes(0, bytes);
        std::memcpy(mapped.data(), source.data(), bytes);
        source_buffer->unmap();
    }

    // Destination texture: sampled (required by the encoder's post-copy
    // SHADER_READ_ONLY transition) + transfer_dst (copy target) + transfer_src
    // (so read_texture_rgba8 can blit it out).
    const std::shared_ptr<erhe::graphics::Texture> destination = std::make_shared<erhe::graphics::Texture>(
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
            .debug_label = erhe::utility::Debug_label{"copy_from_buffer destination"}
        }
    );

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
            // source_bytes_per_image = width*height*4 == whole image; copy the full extent.
            blit.copy_from_buffer(
                source_buffer.get(),
                0,                                                      // source_offset
                static_cast<std::uintptr_t>(width) * 4u,               // source_bytes_per_row
                static_cast<std::uintptr_t>(bytes),                    // source_bytes_per_image
                glm::ivec3{width, height, 1},                          // source_size
                destination.get(),
                0,                                                     // destination_slice
                0,                                                     // destination_level
                glm::ivec3{0, 0, 0}                                    // destination_origin
            );
            // copy_from_buffer leaves the image tracked + physically in
            // SHADER_READ_ONLY_OPTIMAL; read_texture_rgba8 needs transfer_src_optimal.
            command_buffer.transition_texture_layout(*destination, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*destination);
    ASSERT_EQ(pixels.size(), source.size());

    int mismatches = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (pixels[i] != source[i]) {
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0) << mismatches << " of " << source.size() << " bytes differed after buffer->texture copy + readback";
}

} // namespace erhe::graphics::test
