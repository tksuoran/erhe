#pragma once

#include "erhe/physics/jolt/jolt_collision_shape.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class Jolt_convex_hull_collision_shape
    : public Jolt_collision_shape
{
public:
    Jolt_convex_hull_collision_shape(const float* points, const int numPoints, const int stride);
};

} // namespace erhe::physics
