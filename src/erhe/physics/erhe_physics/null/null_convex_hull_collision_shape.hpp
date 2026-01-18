#pragma once

#include "erhe_physics/null/null_collision_shape.hpp"

namespace erhe::physics {

class Null_convex_hull_collision_shape : public Null_collision_shape
{
public:
    Null_convex_hull_collision_shape(const float* points, const int numPoints, const int stride);
};

} // namespace erhe::physics
