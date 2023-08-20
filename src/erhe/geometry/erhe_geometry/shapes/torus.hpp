#pragma once

#include "erhe_geometry/geometry.hpp"

namespace erhe::geometry::shapes
{

[[nodiscard]] auto make_torus(
    double major_radius,
    double minor_radius,
    int    major_axis_steps,
    int    minor_axis_steps
) -> Geometry;

[[nodiscard]] auto torus_volume(
    float major_radius,
    float minor_radius
) -> float;

} // namespace erhe::geometry::shapes
