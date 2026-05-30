#include "gpu_test_fixture.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace erhe::graphics::test {

namespace {

[[nodiscard]] auto to_unorm8(const double value) -> int
{
    const double clamped = (value < 0.0) ? 0.0 : ((value > 1.0) ? 1.0 : value);
    return static_cast<int>(std::lround(clamped * 255.0));
}

} // namespace

// Milestone 2: an offscreen render pass that only clears a color target to a
// known value. No swapchain, no surface, no draw. Read the result back and
// assert every texel matches the clear color (within unorm rounding). This
// exercises the offscreen Render_pass (Clear/Store -> transfer_src_optimal)
// plus the texture -> buffer readback path.
TEST_F(Gpu_test, clear_color_offscreen_readback)
{
    constexpr int width  = 16;
    constexpr int height = 16;

    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);

    // Four distinct channel values so a channel-order mistake is unambiguous:
    // {255, 128, 64, 191}.
    const std::array<double, 4> clear_value{ 1.0, 0.5, 0.25, 0.75 };

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass_descriptor descriptor{};
            descriptor.color_attachments[0].texture      = color_target.get();
            descriptor.color_attachments[0].clear_value  = clear_value;
            descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
            descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
            descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
            descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
            descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
            descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
            descriptor.render_target_width  = width;
            descriptor.render_target_height = height;
            descriptor.debug_label = erhe::utility::Debug_label{"M2 clear color"};

            erhe::graphics::Render_pass render_pass{device(), descriptor};
            // Clear-only: the clear fires on render-pass begin; no draw is recorded.
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    const int expected_r = to_unorm8(clear_value[0]);
    const int expected_g = to_unorm8(clear_value[1]);
    const int expected_b = to_unorm8(clear_value[2]);
    const int expected_a = to_unorm8(clear_value[3]);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            EXPECT_NEAR(static_cast<int>(pixels[index + 0]), expected_r, 1) << "red at "   << x << "," << y;
            EXPECT_NEAR(static_cast<int>(pixels[index + 1]), expected_g, 1) << "green at " << x << "," << y;
            EXPECT_NEAR(static_cast<int>(pixels[index + 2]), expected_b, 1) << "blue at "  << x << "," << y;
            EXPECT_NEAR(static_cast<int>(pixels[index + 3]), expected_a, 1) << "alpha at " << x << "," << y;
        }
    }
}

} // namespace erhe::graphics::test
