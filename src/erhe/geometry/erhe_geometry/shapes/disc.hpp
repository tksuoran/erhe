#pragma once

#include "erhe_geometry/geometry.hpp"

namespace erhe::geometry::shapes {

[[nodiscard]] auto make_disc(double outer_radius, double inner_radius, int slice_count, int stack_count) -> Geometry;
[[nodiscard]] auto make_disc(
    double outer_radius,
    double inner_radius,
    int    slice_count,
    int    stack_count,
    int    slice_begin,
    int    slice_end,
    int    stack_begin,
    int    stack_end
) -> Geometry;

} // namespace erhe::geometry::shapes
