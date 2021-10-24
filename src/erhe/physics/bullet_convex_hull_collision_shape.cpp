#include "erhe/physics/bullet_convex_hull_collision_shape.hpp"
#include "erhe/physics/glm_conversions.hpp"

namespace erhe::physics
{

auto ICollision_shape::create_convex_hull_shape(const float* points, const int numPoints, const int stride) -> ICollision_shape*
{
    return new Bullet_convex_hull_collision_shape(points, numPoints, stride);
}

auto ICollision_shape::create_convex_hull_shape_shared(const float* points, const int numPoints, const int stride) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Bullet_convex_hull_collision_shape>(points, numPoints, stride);
}


Bullet_convex_hull_collision_shape::Bullet_convex_hull_collision_shape(const float* points, const int numPoints, const int stride)
    : Bullet_collision_shape{&m_convex_hull_shape}
    , m_convex_hull_shape{
        reinterpret_cast<const btScalar*>(points),
        numPoints,
        stride}
{
    m_convex_hull_shape.initializePolyhedralFeatures();
}

} // namespace erhe::physics
