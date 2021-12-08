#pragma once

#include "erhe/geometry/geometry.hpp"

namespace erhe::geometry::shapes
{

[[nodiscard]] auto make_cuboctahedron(const double radius) -> Geometry;
[[nodiscard]] auto make_dodecahedron (const double radius) -> Geometry;
[[nodiscard]] auto make_icosahedron  (const double radius) -> Geometry;
[[nodiscard]] auto make_octahedron   (const double radius) -> Geometry;
[[nodiscard]] auto make_tetrahedron  (const double radius) -> Geometry;
[[nodiscard]] auto make_cube         (const double radius) -> Geometry;

} // namespace erhe::geometry::shapes
