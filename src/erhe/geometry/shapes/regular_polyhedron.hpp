#pragma once

#include "erhe/geometry/geometry.hpp"

namespace erhe::geometry::shapes
{

auto make_cuboctahedron(double radius)
-> Geometry;

auto make_dodecahedron(double radius)
-> Geometry;

auto make_icosahedron(double radius)
-> Geometry;

auto make_octahedron(double radius)
-> Geometry;

auto make_tetrahedron(double radius)
-> Geometry;

auto make_cube(double radius)
-> Geometry;

} // namespace erhe::geometry::shapes
