#pragma once

#include "erhe/physics/jolt/jolt_collision_shape.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class Jolt_uniform_scaling_shape
    : public Jolt_collision_shape
{
public:
    Jolt_uniform_scaling_shape(ICollision_shape* shape, const float scale)
        //: m_shape{shape}
        //, m_scale{scale}
    {
        static_cast<void>(shape);
        static_cast<void>(scale);
    }

private:
    //ICollision_shape* m_shape{nullptr};
    //float             m_scale{1.0f};
};

} // namespace erhe::physics
