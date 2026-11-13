#pragma once

#include "erhe_physics/jolt/jolt_collision_shape.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>

namespace erhe::physics {

class Jolt_convex_hull_collision_shape : public Jolt_collision_shape
{
public:
    // The shape Jolt built from settings (create_convex_hull_shape() refuses a point set Jolt does not take).
    Jolt_convex_hull_collision_shape(JPH::Ref<JPH::ConvexHullShapeSettings> settings, JPH::RefConst<JPH::Shape> shape);

    [[nodiscard]] auto get_shape_settings() -> JPH::ShapeSettings&         override;
    [[nodiscard]] auto describe          () const -> std::string            override;
    [[nodiscard]] auto get_shape_type    () const -> Collision_shape_type   override;

private:
    JPH::Ref<JPH::ConvexHullShapeSettings> m_shape_settings;
};

} // namespace erhe::physics
