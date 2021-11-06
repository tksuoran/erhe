#pragma once

#include "erhe/physics/null/null_collision_shape.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class Null_uniform_scaling_shape
    : public Null_collision_shape
{
public:
    Null_uniform_scaling_shape(ICollision_shape* shape, const float scale)
        : m_shape{shape}
        , m_scale{scale}
    {
    }

private:
    ICollision_shape* m_shape{nullptr};
    float             m_scale{1.0f};
};

} // namespace erhe::physics
