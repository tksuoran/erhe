#pragma once

#include "erhe_physics/jolt/jolt_collision_shape.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

namespace erhe::physics {

// Triangle mesh shape; usable with static / kinematic bodies only (Jolt restriction; callers enforce)
class Jolt_mesh_shape : public Jolt_collision_shape
{
public:
    Jolt_mesh_shape(const float* points, int point_count, int point_stride_bytes, const uint32_t* indices, int triangle_count);

    // Implements ICollision_shape
    [[nodiscard]] auto is_convex         () const -> bool                 override;
    [[nodiscard]] auto get_shape_settings() -> JPH::ShapeSettings&        override;
    [[nodiscard]] auto describe          () const -> std::string          override;
    [[nodiscard]] auto get_shape_type    () const -> Collision_shape_type override;

private:
    JPH::Ref<JPH::MeshShapeSettings> m_shape_settings;
};

} // namespace erhe::physics
