#pragma once

#include "erhe/geometry/geometry.hpp"


namespace erhe::geometry::shapes
{

auto make_disc(double outer_radius,
               double inner_radius,
               int    slice_count,
               int    stack_count)
-> Geometry;

} // namespace erhe::geometry::shapes
