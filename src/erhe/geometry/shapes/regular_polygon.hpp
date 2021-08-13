#pragma once

#include "erhe/geometry/geometry.hpp"

namespace erhe::geometry::shapes
{

auto make_triangle(const double radius)
-> Geometry;

auto make_quad(const double edge)
-> Geometry;


} // namespace erhe::geometry::shapes
