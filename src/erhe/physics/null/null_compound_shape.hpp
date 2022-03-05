#pragma once

#include "erhe/physics/null/null_collision_shape.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe::physics
{

class Null_compound_shape
    : public Null_collision_shape
{
public:
    Null_compound_shape(const Compound_shape_create_info& create_info)
    {
        static_cast<void>(create_info);
    }

    // Implements ICollision_shape
    auto is_convex() const -> bool override
    {
        return false;
    }
};

} // namespace erhe::physics
