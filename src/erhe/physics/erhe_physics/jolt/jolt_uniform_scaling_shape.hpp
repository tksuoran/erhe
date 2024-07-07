#pragma once

#include "erhe_physics/jolt/jolt_collision_shape.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>

namespace erhe::physics {

class Jolt_uniform_scaling_shape : public Jolt_collision_shape
{
public:
    Jolt_uniform_scaling_shape(ICollision_shape* shape, float scale);

    auto get_shape_settings() -> JPH::ShapeSettings& override;
    auto describe          () const -> std::string   override;

private:
    JPH::Ref<JPH::ScaledShapeSettings> m_shape_settings;
};

} // namespace erhe::physics
