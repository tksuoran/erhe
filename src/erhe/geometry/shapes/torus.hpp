#pragma once

#include "erhe/geometry/geometry.hpp"

namespace erhe::geometry::shapes
{

[[nodiscard]] auto make_torus(
    const double major_radius,
    const double minor_radius,
    const int    major_axis_steps,
    const int    minor_axis_steps
) -> Geometry;

[[nodiscard]] auto torus_volume(
    const float major_radius,
    const float minor_radius
) -> float;

} // namespace erhe::geometry::shapes
