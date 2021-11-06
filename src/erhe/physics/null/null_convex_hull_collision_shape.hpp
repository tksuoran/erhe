#pragma once

#include "erhe/physics/null/null_collision_shape.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class Null_convex_hull_collision_shape
    : public Null_collision_shape
{
public:
    Null_convex_hull_collision_shape(const float* points, const int numPoints, const int stride);
};

} // namespace erhe::physics
