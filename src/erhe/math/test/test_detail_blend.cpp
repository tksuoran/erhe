#include "erhe_math/detail_blend.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace {

using erhe::math::detail_blend;

// The grid of doc/plans/detail_blend_weight.md B3.3: every parameter value is
// a grid point, so each property holds over all of them.
constexpr int c_steps = 40;

constexpr std::array<float, 5> c_influences   {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
constexpr std::array<float, 6> c_widths       {0.0f, 1.0e-5f, 0.01f, 0.2f, 0.6f, 1.0f};
constexpr std::array<float, 4> c_pixel_widths {0.0f, 0.001f, 0.05f, 0.5f};

// Rounding of the last mix step may move a value by an ulp.
constexpr float c_rounding = 1.0e-6f;

[[nodiscard]] auto grid(const int i) -> float
{
    return static_cast<float>(i) / static_cast<float>(c_steps);
}

static_assert(detail_blend(0.0f, 0.9f, 1.0f, 0.2f, 0.0f) == 0.0f);
static_assert(detail_blend(1.0f, 0.1f, 1.0f, 0.2f, 0.0f) == 1.0f);

TEST(Detail_blend, weight_zero_gives_zero_and_weight_one_gives_one)
{
    for (const float influence : c_influences) {
        for (const float width : c_widths) {
            for (const float pixel_width : c_pixel_widths) {
                for (int j = 0; j <= c_steps; ++j) {
                    const float h = grid(j);
                    EXPECT_EQ(detail_blend(0.0f, h, influence, width, pixel_width), 0.0f) << "h " << h << " width " << width;
                    EXPECT_EQ(detail_blend(1.0f, h, influence, width, pixel_width), 1.0f) << "h " << h << " width " << width;
                }
            }
        }
    }
}

TEST(Detail_blend, result_stays_in_the_unit_range_and_rises_with_weight_and_height)
{
    for (const float influence : c_influences) {
        for (const float width : c_widths) {
            for (const float pixel_width : c_pixel_widths) {
                for (int i = 0; i <= c_steps; ++i) {
                    for (int j = 0; j <= c_steps; ++j) {
                        const float w = grid(i);
                        const float h = grid(j);
                        const float value = detail_blend(w, h, influence, width, pixel_width);
                        EXPECT_GE(value, 0.0f);
                        EXPECT_LE(value, 1.0f);
                        if (i < c_steps) {
                            EXPECT_GE(detail_blend(grid(i + 1), h, influence, width, pixel_width), value - c_rounding) << "w " << w << " h " << h;
                        }
                        if (j < c_steps) {
                            EXPECT_GE(detail_blend(w, grid(j + 1), influence, width, pixel_width), value - c_rounding) << "w " << w << " h " << h;
                        }
                    }
                }
            }
        }
    }
}

TEST(Detail_blend, greatest_height_reaches_one_first)
{
    // The least grid weight at which the result reaches 1 falls as the height rises.
    for (const float width : c_widths) {
        int previous_threshold = c_steps + 1;
        for (int j = 0; j <= c_steps; ++j) {
            const float h = grid(j);
            int threshold = c_steps + 1;
            for (int i = 0; i <= c_steps; ++i) {
                if (detail_blend(grid(i), h, 1.0f, width, 0.0f) >= 1.0f) {
                    threshold = i;
                    break;
                }
            }
            EXPECT_LE(threshold, previous_threshold) << "h " << h << " width " << width;
            previous_threshold = threshold;
        }
    }
}

TEST(Detail_blend, zero_width_gives_a_hard_edge_at_one_minus_weight)
{
    // The effective width is then 1e-4: outside that margin of h = 1 - w the result is 0 or 1.
    constexpr float margin = 2.0e-4f;
    for (int i = 1; i < c_steps; ++i) {
        const float w = grid(i);
        for (int j = 0; j <= 1000; ++j) {
            const float h = static_cast<float>(j) / 1000.0f;
            const float value = detail_blend(w, h, 1.0f, 0.0f, 0.0f);
            if (h < (1.0f - w - margin)) {
                EXPECT_EQ(value, 0.0f) << "w " << w << " h " << h;
            } else if (h > (1.0f - w + margin)) {
                EXPECT_EQ(value, 1.0f) << "w " << w << " h " << h;
            }
        }
    }
}

TEST(Detail_blend, pixel_width_widens_a_hard_edge_to_a_ramp)
{
    // With width 0 the ramp spans the pixel width: half way up it, the result is half way.
    const float pixel_width = 0.1f;
    const float w           = 0.5f;
    const float h_middle    = 1.0f - w - (w * pixel_width) + (0.5f * pixel_width);
    EXPECT_NEAR(detail_blend(w, h_middle, 1.0f, 0.0f, pixel_width), 0.5f, 1.0e-5f);
    EXPECT_GT(detail_blend(w, h_middle + 0.02f, 1.0f, 0.0f, pixel_width), 0.5f);
    EXPECT_LT(detail_blend(w, h_middle - 0.02f, 1.0f, 0.0f, pixel_width), 0.5f);
}

TEST(Detail_blend, unit_width_gives_a_soft_ramp)
{
    // At width 1 and weight one half the result is the height itself.
    for (int j = 0; j <= c_steps; ++j) {
        const float h = grid(j);
        EXPECT_NEAR(detail_blend(0.5f, h, 1.0f, 1.0f, 0.0f), h, 1.0e-6f) << "h " << h;
    }
}

TEST(Detail_blend, zero_influence_gives_the_weight)
{
    for (int i = 0; i <= c_steps; ++i) {
        for (int j = 0; j <= c_steps; ++j) {
            const float w = grid(i);
            EXPECT_EQ(detail_blend(w, grid(j), 0.0f, 0.2f, 0.01f), w);
        }
    }
}

} // namespace
