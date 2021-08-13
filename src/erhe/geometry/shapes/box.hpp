#pragma once

#include "erhe/geometry/geometry.hpp"
#include <cmath>

namespace erhe::geometry::shapes
{

auto make_box(const double x_size, const double y_size, const double z_size)
-> Geometry;

auto make_box(const float min_x, const float max_x, const float min_y, const float max_y, const float min_z, const float max_z)
-> Geometry;

auto make_box(const double r)
-> Geometry;

auto make_box(const glm::vec3 size, const glm::ivec3 div, const float p = 1.0f)
-> Geometry;

} // namespace erhe::geometry::shapes
