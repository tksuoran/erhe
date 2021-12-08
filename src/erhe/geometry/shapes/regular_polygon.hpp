#pragma once

#include "erhe/geometry/geometry.hpp"

namespace erhe::geometry::shapes
{

[[nodiscard]] auto make_triangle(const double radius) -> Geometry;
[[nodiscard]] auto make_quad    (const double edge) -> Geometry;

} // namespace erhe::geometry::shapes
