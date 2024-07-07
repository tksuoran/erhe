#pragma once

#include "erhe_geometry/geometry.hpp"

namespace erhe::geometry::shapes {

[[nodiscard]] auto make_triangle (double radius) -> Geometry;
[[nodiscard]] auto make_quad     (double edge) -> Geometry;
[[nodiscard]] auto make_rectangle(double width, double height, bool front_face = true, bool back_face = true) -> Geometry;

} // namespace erhe::geometry::shapes
