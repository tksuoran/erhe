#pragma once

#include "erhe_physics/null/null_collision_shape.hpp"

namespace erhe::physics {

class Null_convex_hull_collision_shape : public Null_collision_shape
{
public:
    Null_convex_hull_collision_shape(const float* points, const int numPoints, const int stride);

    [[nodiscard]] auto get_shape_type() const -> Collision_shape_type override { return Collision_shape_type::e_convex_hull; }
};

} // namespace erhe::physics
