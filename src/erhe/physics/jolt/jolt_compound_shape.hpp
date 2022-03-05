#pragma once

#include "erhe/physics/jolt/jolt_collision_shape.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe::physics
{

class Jolt_compound_shape
    : public Jolt_collision_shape
{
public:
    explicit Jolt_compound_shape(const Compound_shape_create_info& create_info);

    // Implements ICollision_shape
    auto is_convex() const -> bool override
    {
        return false;
    }
};

} // namespace erhe::physics
