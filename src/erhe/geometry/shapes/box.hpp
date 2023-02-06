#pragma once

#include "erhe/geometry/geometry.hpp"
#include <cmath>

namespace erhe::geometry::shapes
{

[[nodiscard]] auto make_box(
    double x_size,
    double y_size,
    double z_size
) -> Geometry;

[[nodiscard]] auto make_box(
    float min_x,
    float max_x,
    float min_y,
    float max_y,
    float min_z,
    float max_z
) -> Geometry;

[[nodiscard]] auto make_box(double r) -> Geometry;

[[nodiscard]] auto make_box(
    glm::vec3  size,
    glm::ivec3 div,
    float      p = 1.0f
) -> Geometry;

} // namespace erhe::geometry::shapes
