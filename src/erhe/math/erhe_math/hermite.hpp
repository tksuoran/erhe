#pragma once

#include <algorithm>
#include <cstddef>
#include <span>

namespace erhe::math {

// Cubic Hermite segment from (x0, y0) to (x1, y1) with per-x-unit slopes at
// both ends (glTF 2.0 appendix C form, the Bezier form with handles at one
// third of the span is the same curve). t is the normalized position in
// [0, 1] and dx = x1 - x0.
[[nodiscard]] constexpr auto hermite(
    const float t,
    const float dx,
    const float y0,
    const float slope0_out,
    const float y1,
    const float slope1_in
) -> float
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return
        ((2.0f * t3) - (3.0f * t2) + 1.0f) * y0 +
        (dx * (t3 - (2.0f * t2) + t))     * slope0_out +
        ((-2.0f * t3) + (3.0f * t2))      * y1 +
        (dx * (t3 - t2))                  * slope1_in;
}

// The Catmull-Rom (central finite difference) slope at point i of a curve
// sampled at strictly increasing xs: one-sided at the ends, zero for a
// single point. The minimum x separation guards a collapsed pair.
[[nodiscard]] inline auto catmull_rom_slope(
    const std::span<const float> xs,
    const std::span<const float> ys,
    const std::size_t            i,
    const float                  min_dx = 1.0e-5f
) -> float
{
    const std::size_t n = xs.size();
    if ((n < 2) || (i >= n)) {
        return 0.0f;
    }
    const std::size_t a = (i == 0) ? 0 : (i - 1);
    const std::size_t b = (i == (n - 1)) ? (n - 1) : (i + 1);
    return (ys[b] - ys[a]) / std::max(min_dx, xs[b] - xs[a]);
}

} // namespace erhe::math
