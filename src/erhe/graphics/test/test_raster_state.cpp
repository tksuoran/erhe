#include "gpu_test_fixture.hpp"

#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace erhe::graphics::test {

namespace {

[[nodiscard]] auto count_green(const std::vector<uint8_t>& pixels) -> int
{
    int n = 0;
    for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
        if ((pixels[i + 1] >= 250) && (pixels[i + 0] <= 5) && (pixels[i + 2] <= 5)) {
            ++n;
        }
    }
    return n;
}

} // namespace

// Face culling. The fullscreen triangle has one winding, so it is back-facing
// under exactly one front-face convention: cull_mode_back_cw and
// cull_mode_back_ccw must therefore cull it in exactly one of the two cases,
// while cull_mode_none always draws it. Robust to the actual winding / Y
// orientation.
TEST_F(Gpu_test, rasterization_face_culling)
{
    const std::array<double, 4> black{ 0.0, 0.0, 0.0, 1.0 };
    const char* green = "vec4(0.0, 1.0, 0.0, 1.0)";

    const int g_none = count_green(draw_fullscreen_triangle(
        green, erhe::graphics::Rasterization_state::cull_mode_none, erhe::graphics::Color_blend_state::color_blend_disabled, black));
    const int g_cw = count_green(draw_fullscreen_triangle(
        green, erhe::graphics::Rasterization_state::cull_mode_back_cw, erhe::graphics::Color_blend_state::color_blend_disabled, black));
    const int g_ccw = count_green(draw_fullscreen_triangle(
        green, erhe::graphics::Rasterization_state::cull_mode_back_ccw, erhe::graphics::Color_blend_state::color_blend_disabled, black));

    EXPECT_GT(g_none, 0) << "cull_mode_none should draw the triangle";
    EXPECT_TRUE(((g_cw == 0) && (g_ccw > 0)) || ((g_ccw == 0) && (g_cw > 0)))
        << "exactly one of cull_back_cw / cull_back_ccw should cull the triangle"
        << " (none=" << g_none << " cw=" << g_cw << " ccw=" << g_ccw << ")";
    EXPECT_EQ(g_none, (g_cw > g_ccw) ? g_cw : g_ccw) << "the non-culled cull-back case should match cull_none coverage";
}

// Color write mask. Draw white with green+blue writes disabled over a black
// clear; only red and alpha should be written.
TEST_F(Gpu_test, color_write_mask)
{
    erhe::graphics::Color_blend_state mask_state; // enabled=false; write_mask all-true by default
    mask_state.write_mask.green = false;
    mask_state.write_mask.blue  = false;

    const std::array<double, 4> black{ 0.0, 0.0, 0.0, 1.0 };
    const std::vector<uint8_t> pixels = draw_fullscreen_triangle(
        "vec4(1.0, 1.0, 1.0, 1.0)",
        erhe::graphics::Rasterization_state::cull_mode_none,
        mask_state,
        black
    );

    ASSERT_FALSE(pixels.empty());
    int bad = 0;
    for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
        const int r = pixels[i + 0];
        const int g = pixels[i + 1];
        const int b = pixels[i + 2];
        const int a = pixels[i + 3];
        const bool ok = (r >= 253) && (g <= 2) && (b <= 2) && (a >= 253);
        if (!ok) {
            ++bad;
        }
    }
    EXPECT_EQ(bad, 0) << bad << " texels were not {255,0,0,255}; green/blue writes should have been masked off";
}

} // namespace erhe::graphics::test
