#pragma once

#include "erhe/geometry/geometry.hpp"

namespace erhe::geometry::shapes
{

auto make_triangle(double radius)
-> Geometry;

auto make_quad(double edge)
-> Geometry;


} // namespace erhe::geometry::shapes
