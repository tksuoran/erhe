#pragma once

#include "erhe_geometry/geometry.hpp"

namespace erhe::geometry::shapes {

[[nodiscard]] auto make_cuboctahedron(double radius) -> Geometry;
[[nodiscard]] auto make_dodecahedron (double radius) -> Geometry;
[[nodiscard]] auto make_icosahedron  (double radius) -> Geometry;
[[nodiscard]] auto make_octahedron   (double radius) -> Geometry;
[[nodiscard]] auto make_tetrahedron  (double radius) -> Geometry;
[[nodiscard]] auto make_cube         (double radius) -> Geometry;

} // namespace erhe::geometry::shapes
