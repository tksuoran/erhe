#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace erhe::graphics::test {

namespace {

// Distinct bytes per texel and channel, so a misplaced or channel-swapped
// texel cannot match.
auto make_pattern(const int width, const int height, const uint8_t seed) -> std::vector<uint8_t>
{
    std::vector<uint8_t> pattern(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)) * 4u;
            pattern[i + 0] = static_cast<uint8_t>(((x * 29) + seed)            & 0xff);
            pattern[i + 1] = static_cast<uint8_t>(((y * 37) + (seed * 3) + 1) & 0xff);
            pattern[i + 2] = static_cast<uint8_t>(((x + y) * 11 + 101)       & 0xff);
            pattern[i + 3] = static_cast<uint8_t>((((x * 5) + (y * 7)) + 60) & 0xff);
        }
    }
    return pattern;
}

// A render pass over one color target that the test never begins: the blit
// only reads its color attachment 0. transfer_src before and after, the
// make_color_target convention.
auto make_pass(erhe::graphics::Device& graphics_device, erhe::graphics::Texture& texture, const char* debug_label)
    -> std::unique_ptr<erhe::graphics::Render_pass>
{
    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = &texture;
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Load;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = texture.get_width();
    descriptor.render_target_height = texture.get_height();
    descriptor.debug_label          = erhe::utility::Debug_label{debug_label};
    return std::make_unique<erhe::graphics::Render_pass>(graphics_device, descriptor);
}

} // namespace

class Blit_framebuffer_test : public Gpu_test
{
protected:
    // Seeds both targets, blits, leaves the destination in
    // transfer_src_optimal for the readback.
    void run_blit(
        erhe::graphics::Texture&    source_texture,
        const std::vector<uint8_t>& source_texels,
        erhe::graphics::Texture&    destination_texture,
        const std::vector<uint8_t>& destination_texels,
        const glm::ivec2            source_origin,
        const glm::ivec2            source_size,
        const glm::ivec2            destination_origin
    )
    {
        erhe::graphics::Device& graphics_device = device();
        seed_subresource_rgba8(source_texture,      0, 0, source_texels);
        seed_subresource_rgba8(destination_texture, 0, 0, destination_texels);
        const std::unique_ptr<erhe::graphics::Render_pass> source_pass      = make_pass(graphics_device, source_texture,      "blit_framebuffer source");
        const std::unique_ptr<erhe::graphics::Render_pass> destination_pass = make_pass(graphics_device, destination_texture, "blit_framebuffer destination");
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                {
                    erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
                    blit.blit_framebuffer(*source_pass, source_origin, source_size, *destination_pass, destination_origin);
                }
                command_buffer.transition_texture_layout(destination_texture, erhe::graphics::Image_layout::transfer_src_optimal);
            }
        );
    }
};

TEST(Blit_framebuffer_region, clips_against_both_attachments)
{
    // Inside: unchanged.
    {
        const Blit_framebuffer_region r = clip_blit_framebuffer_region({1, 2}, {4, 3}, {8, 8}, {2, 1}, {8, 8});
        EXPECT_EQ(r.source_origin,      glm::ivec2(1, 2));
        EXPECT_EQ(r.destination_origin, glm::ivec2(2, 1));
        EXPECT_EQ(r.size,               glm::ivec2(4, 3));
    }
    // Negative destination origin: both origins move by the clipped amount.
    {
        const Blit_framebuffer_region r = clip_blit_framebuffer_region({0, 0}, {10, 10}, {10, 10}, {-3, -2}, {6, 6});
        EXPECT_EQ(r.source_origin,      glm::ivec2(3, 2));
        EXPECT_EQ(r.destination_origin, glm::ivec2(0, 0));
        EXPECT_EQ(r.size,               glm::ivec2(6, 6));
    }
    // Source end past the source attachment.
    {
        const Blit_framebuffer_region r = clip_blit_framebuffer_region({5, 0}, {8, 4}, {8, 4}, {0, 0}, {16, 16});
        EXPECT_EQ(r.size, glm::ivec2(3, 4));
    }
    // Disjoint: nothing.
    {
        const Blit_framebuffer_region r = clip_blit_framebuffer_region({0, 0}, {4, 4}, {4, 4}, {9, 0}, {8, 8});
        EXPECT_EQ(r.size, glm::ivec2(0, 0));
    }
}

// Same format, render passes of different sizes, offset origins: the copied
// rectangle is clipped by the destination, texels outside it keep their
// seed.
TEST_F(Blit_framebuffer_test, offset_clipped)
{
    constexpr int source_width       = 16;
    constexpr int source_height      = 12;
    constexpr int destination_width  = 10;
    constexpr int destination_height = 14;
    const glm::ivec2 source_origin     {3, 2};
    const glm::ivec2 source_size       {12, 9};
    const glm::ivec2 destination_origin{2, 4};
    // Clipped by the destination width: 10 - 2 = 8 columns; all 9 rows fit.
    const glm::ivec2 copied            {8, 9};

    const std::shared_ptr<erhe::graphics::Texture> source_texture      = make_color_target(source_width,      source_height,      erhe::dataformat::Format::format_8_vec4_unorm, true);
    const std::shared_ptr<erhe::graphics::Texture> destination_texture = make_color_target(destination_width, destination_height, erhe::dataformat::Format::format_8_vec4_unorm, true);
    const std::vector<uint8_t> source_texels      = make_pattern(source_width,      source_height,      7);
    const std::vector<uint8_t> destination_texels = make_pattern(destination_width, destination_height, 200);

    run_blit(*source_texture, source_texels, *destination_texture, destination_texels, source_origin, source_size, destination_origin);

    const std::vector<uint8_t> actual = read_texture_rgba8(*destination_texture);
    ASSERT_EQ(actual.size(), destination_texels.size());
    int mismatches = 0;
    for (int y = 0; y < destination_height; ++y) {
        for (int x = 0; x < destination_width; ++x) {
            const glm::ivec2 d{x - destination_origin.x, y - destination_origin.y};
            const bool inside = (d.x >= 0) && (d.y >= 0) && (d.x < copied.x) && (d.y < copied.y);
            const std::size_t actual_index = ((static_cast<std::size_t>(y) * destination_width) + static_cast<std::size_t>(x)) * 4u;
            const std::size_t expect_index = inside
                ? ((static_cast<std::size_t>(source_origin.y + d.y) * source_width) + static_cast<std::size_t>(source_origin.x + d.x)) * 4u
                : actual_index;
            const std::vector<uint8_t>& expected = inside ? source_texels : destination_texels;
            if (std::memcmp(&actual[actual_index], &expected[expect_index], 4) != 0) {
                if (mismatches < 8) {
                    ADD_FAILURE() << "texel (" << x << ", " << y << ") " << (inside ? "inside" : "outside") << " the copied rectangle differs";
                }
                ++mismatches;
            }
        }
    }
    EXPECT_EQ(mismatches, 0);
}

// RGBA8 to BGRA8: the blit converts the format (a texel copy would land the
// RGBA bytes unchanged, swapping red and blue).
TEST_F(Blit_framebuffer_test, format_conversion)
{
    constexpr int width  = 8;
    constexpr int height = 6;

    const std::shared_ptr<erhe::graphics::Texture> source_texture      = make_color_target(width, height, erhe::dataformat::Format::format_8_vec4_unorm,      true);
    const std::shared_ptr<erhe::graphics::Texture> destination_texture = make_color_target(width, height, erhe::dataformat::Format::format_8_vec4_bgra_unorm, true);
    const std::vector<uint8_t> source_texels      = make_pattern(width, height, 19);
    const std::vector<uint8_t> destination_texels(source_texels.size(), uint8_t{0});

    run_blit(*source_texture, source_texels, *destination_texture, destination_texels, glm::ivec2{0, 0}, glm::ivec2{width, height}, glm::ivec2{0, 0});

    const std::vector<std::byte> actual = read_texture_color_bytes(*destination_texture, 4);
    ASSERT_EQ(actual.size(), source_texels.size());
    int mismatches = 0;
    for (std::size_t i = 0; i < source_texels.size(); i += 4) {
        const uint8_t b = static_cast<uint8_t>(actual[i + 0]);
        const uint8_t g = static_cast<uint8_t>(actual[i + 1]);
        const uint8_t r = static_cast<uint8_t>(actual[i + 2]);
        const uint8_t a = static_cast<uint8_t>(actual[i + 3]);
        if ((r != source_texels[i + 0]) || (g != source_texels[i + 1]) || (b != source_texels[i + 2]) || (a != source_texels[i + 3])) {
            if (mismatches < 8) {
                ADD_FAILURE() << "texel " << (i / 4) << " rgba " << int{r} << " " << int{g} << " " << int{b} << " " << int{a}
                              << " expected " << int{source_texels[i + 0]} << " " << int{source_texels[i + 1]} << " " << int{source_texels[i + 2]} << " " << int{source_texels[i + 3]};
            }
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0);
}

} // namespace erhe::graphics::test
