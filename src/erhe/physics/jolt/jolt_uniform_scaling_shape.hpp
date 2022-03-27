#pragma once

#include "erhe/physics/jolt/jolt_collision_shape.hpp"

#include <Jolt.h>
#include <Physics/Collision/Shape/ScaledShape.h>

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class Jolt_uniform_scaling_shape
    : public Jolt_collision_shape
{
public:
    Jolt_uniform_scaling_shape(ICollision_shape* shape, const float scale)
        : m_shape_settings{
            &(reinterpret_cast<Jolt_collision_shape*>(shape)->get_shape_settings()),
            JPH::Vec3Arg{scale, scale, scale}
        }
    {
	    auto result = m_shape_settings.Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    auto get_shape_settings() -> JPH::ShapeSettings& override
    {
        return m_shape_settings;
    }

private:
    JPH::ScaledShapeSettings m_shape_settings;
};

} // namespace erhe::physics
