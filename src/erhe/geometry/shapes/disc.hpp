#pragma once

#include "erhe/geometry/geometry.hpp"


namespace erhe::geometry::shapes
{

auto make_disc(
    const double outer_radius,
    const double inner_radius,
    const int    slice_count,
    const int    stack_count
) -> Geometry;

} // namespace erhe::geometry::shapes
