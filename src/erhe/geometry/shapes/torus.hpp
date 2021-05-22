#pragma once

#include "erhe/geometry/geometry.hpp"
namespace erhe::geometry::shapes
{

auto make_torus(double major_radius, double minor_radius, int major_axis_steps, int minor_axis_steps)
-> Geometry;

auto torus_volume(float major_radius, float minor_radius) -> float;

} // namespace erhe::geometry::shapes
