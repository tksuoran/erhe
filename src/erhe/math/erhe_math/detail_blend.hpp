#pragma once

#include <algorithm>

namespace erhe::math {

// The detail weight of weight w and combined height h (doc/erhe/math.md
// "Detail blend"); the CPU twin of erhe_detail_blend() in
// res/shaders/erhe_detail_blend.glsl, in the same arithmetic.
[[nodiscard]] constexpr auto detail_blend(
    const float w,
    const float h,
    const float influence,
    const float width,
    const float pixel_width
) -> float
{
    const float s = std::max(std::max(width, pixel_width), 1.0e-4f);
    const float d = std::clamp(((h - 1.0f + w) / s) + w, 0.0f, 1.0f);
    return w + ((d - w) * influence);
}

} // namespace erhe::math
