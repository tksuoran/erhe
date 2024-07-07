#pragma once

#include "erhe_geometry/geometry.hpp"

namespace erhe::geometry::shapes {

[[nodiscard]] auto make_sphere(double radius, unsigned int slice_count, unsigned int stack_division) -> Geometry;

} // namespace erhe::geometry::shapes
