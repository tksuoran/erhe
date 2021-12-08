#pragma once

#include "erhe/geometry/geometry.hpp"

namespace erhe::geometry::shapes
{

[[nodiscard]] auto make_sphere(
    const double       radius,
    const unsigned int slice_count,
    const unsigned int stack_division
) -> Geometry;

} // namespace erhe::geometry::shapes
