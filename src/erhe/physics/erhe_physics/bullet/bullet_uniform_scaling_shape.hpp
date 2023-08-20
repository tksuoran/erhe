#pragma once

#include "erhe_physics/bullet/bullet_collision_shape.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class Bullet_uniform_scaling_shape
    : public Bullet_collision_shape
{
public:
    Bullet_uniform_scaling_shape(
        ICollision_shape* shape,
        const float       scale
    );

private:
    btUniformScalingShape m_uniform_scaling_shape;
};

} // namespace erhe::physics
