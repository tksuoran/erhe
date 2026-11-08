#include "erhe_math/hermite.hpp"

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace {

TEST(Hermite, EndpointsAndLinearSlopes)
{
    // Slopes equal to the chord slope reproduce the straight line.
    const float y0 = 1.0f;
    const float y1 = 3.0f;
    const float dx = 2.0f;
    const float slope = (y1 - y0) / dx;
    EXPECT_FLOAT_EQ(erhe::math::hermite(0.0f, dx, y0, slope, y1, slope), y0);
    EXPECT_FLOAT_EQ(erhe::math::hermite(1.0f, dx, y0, slope, y1, slope), y1);
    EXPECT_NEAR(erhe::math::hermite(0.25f, dx, y0, slope, y1, slope), 1.5f, 1.0e-6f);
    EXPECT_NEAR(erhe::math::hermite(0.5f,  dx, y0, slope, y1, slope), 2.0f, 1.0e-6f);
}

TEST(Hermite, SlopeIsPerXUnit)
{
    // A segment twice as long with the same per-x-unit slopes bends the
    // same amount per x unit: the midpoint overshoot scales with dx.
    const float short_mid = erhe::math::hermite(0.5f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    const float long_mid  = erhe::math::hermite(0.5f, 2.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_NEAR(long_mid, 2.0f * short_mid, 1.0e-6f);
    EXPECT_NEAR(short_mid, 0.0f, 1.0e-6f); // symmetric slopes cancel at the midpoint
}

TEST(CatmullRomSlope, FiniteDifferences)
{
    const std::array<float, 4> xs{0.0f, 1.0f, 3.0f, 4.0f};
    const std::array<float, 4> ys{0.0f, 2.0f, 2.0f, 5.0f};
    EXPECT_FLOAT_EQ(erhe::math::catmull_rom_slope(xs, ys, 0), 2.0f);           // one-sided start
    EXPECT_FLOAT_EQ(erhe::math::catmull_rom_slope(xs, ys, 1), 2.0f / 3.0f);    // (2 - 0) / (3 - 0)
    EXPECT_FLOAT_EQ(erhe::math::catmull_rom_slope(xs, ys, 2), 3.0f / 3.0f);    // (5 - 2) / (4 - 1)
    EXPECT_FLOAT_EQ(erhe::math::catmull_rom_slope(xs, ys, 3), 3.0f);           // one-sided end
}

TEST(CatmullRomSlope, DegenerateInputs)
{
    const std::vector<float> one_x{1.0f};
    const std::vector<float> one_y{7.0f};
    EXPECT_FLOAT_EQ(erhe::math::catmull_rom_slope(one_x, one_y, 0), 0.0f);
    const std::array<float, 2> xs{0.0f, 1.0f};
    const std::array<float, 2> ys{0.0f, 1.0f};
    EXPECT_FLOAT_EQ(erhe::math::catmull_rom_slope(xs, ys, 5), 0.0f); // out of range
}

} // namespace
