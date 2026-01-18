#pragma once

#include "erhe_physics/null/null_collision_shape.hpp"

namespace erhe::physics {

class Null_compound_shape : public Null_collision_shape
{
public:
    explicit Null_compound_shape(const Compound_shape_create_info& create_info)
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
