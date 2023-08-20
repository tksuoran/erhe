#include "erhe_physics/null/null_convex_hull_collision_shape.hpp"

namespace erhe::physics
{

auto ICollision_shape::create_convex_hull_shape(
    const float* points,
    const int    numPoints,
    const int    stride
) -> ICollision_shape*
{
    return new Null_convex_hull_collision_shape(points, numPoints, stride);
}

auto ICollision_shape::create_convex_hull_shape_shared(
    const float* points,
    const int    numPoints,
    const int    stride
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Null_convex_hull_collision_shape>(points, numPoints, stride);
}

Null_convex_hull_collision_shape::Null_convex_hull_collision_shape(
    const float* points,
    const int    numPoints,
    const int    stride
)
{
    static_cast<void>(points);
    static_cast<void>(numPoints);
    static_cast<void>(stride);
}

} // namespace erhe::physics
