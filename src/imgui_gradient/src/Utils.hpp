#pragma once
#include <cmath>
#include "RelativePosition.hpp"

// https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf page 260

namespace ImGG {

namespace internal {

inline auto clamp_position(float position) -> RelativePosition
{
    return RelativePosition{std::fmin(std::fmax(position, 0.f), 1.f)};
}

/// Always returns a number between 0.f and 1.f, even if x is negative.
inline auto fract(float x) -> float
{
    return x - std::floor(x);
}

inline auto modulo(float x, float mod) -> float
{
    return fract(x / mod) * mod;
}

inline auto repeat_position(float position) -> RelativePosition
{
    return RelativePosition{fract(position)};
}

// Applies a mirror transform on position given around 0.f, then repeat it
inline auto mirror_repeat_position(float position) -> RelativePosition
{
    return RelativePosition{1.f - (std::abs(modulo(position, 2.f) - 1.f))};
}

}} // namespace ImGG::internal