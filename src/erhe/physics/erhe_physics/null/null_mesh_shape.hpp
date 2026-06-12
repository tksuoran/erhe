#pragma once

#include "erhe_physics/null/null_collision_shape.hpp"

namespace erhe::physics {

// Triangle mesh shape; usable with static / kinematic bodies only (callers enforce)
class Null_mesh_shape : public Null_collision_shape
{
public:
    Null_mesh_shape(const float* points, int point_count, int point_stride_bytes, const uint32_t* indices, int triangle_count);

    [[nodiscard]] auto is_convex     () const -> bool                 override { return false; }
    [[nodiscard]] auto get_shape_type() const -> Collision_shape_type override { return Collision_shape_type::e_mesh; }
};

} // namespace erhe::physics
