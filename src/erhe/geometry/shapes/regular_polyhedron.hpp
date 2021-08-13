#pragma once

#include "erhe/geometry/geometry.hpp"

namespace erhe::geometry::shapes
{

auto make_cuboctahedron(const double radius)
-> Geometry;

auto make_dodecahedron(const double radius)
-> Geometry;

auto make_icosahedron(const double radius)
-> Geometry;

auto make_octahedron(const double radius)
-> Geometry;

auto make_tetrahedron(const double radius)
-> Geometry;

auto make_cube(const double radius)
-> Geometry;

} // namespace erhe::geometry::shapes
