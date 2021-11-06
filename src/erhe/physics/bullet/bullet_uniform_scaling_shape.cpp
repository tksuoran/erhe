#include "erhe/physics/bullet/bullet_uniform_scaling_shape.hpp"

#include "BulletCollision/CollisionShapes/btUniformScalingShape.h"

namespace erhe::physics
{

auto ICollision_shape::create_uniform_scaling_shape(ICollision_shape* shape, const float scale) -> ICollision_shape*
{
    return new Bullet_uniform_scaling_shape(shape, scale);
}

auto ICollision_shape::create_uniform_scaling_shape_shared(ICollision_shape* shape, const float scale) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Bullet_uniform_scaling_shape>(shape, scale);
}


Bullet_uniform_scaling_shape::Bullet_uniform_scaling_shape(ICollision_shape* shape, const float scale)
    : Bullet_collision_shape{&m_uniform_scaling_shape}
    , m_uniform_scaling_shape{
        static_cast<btConvexShape*>(
            reinterpret_cast<Bullet_collision_shape*>(shape)->get_bullet_collision_shape()
        ),
        scale
    }
{
}

} // namespace erhe::physics
