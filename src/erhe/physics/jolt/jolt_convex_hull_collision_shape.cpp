#include "erhe/physics/jolt/jolt_convex_hull_collision_shape.hpp"

#include <Jolt.h>
#include <Physics/Collision/Shape/ConvexHullShape.h>

namespace erhe::physics
{

auto ICollision_shape::create_convex_hull_shape(
    const float* points,
    const int    numPoints,
    const int    stride
) -> ICollision_shape*
{
    return new Jolt_convex_hull_collision_shape(points, numPoints, stride);
}

auto ICollision_shape::create_convex_hull_shape_shared(
    const float* points,
    const int    numPoints,
    const int    stride
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_convex_hull_collision_shape>(points, numPoints, stride);
}

Jolt_convex_hull_collision_shape::Jolt_convex_hull_collision_shape(
    const float* pointData,
    const int    numPoints,
    const int    stride
)
{
    std::vector<JPH::Vec3> points;
    points.resize(numPoints);
    for (int i = 0; i < numPoints; ++i)
    {
        points[i] = JPH::Vec3{
            pointData[0],
            pointData[1],
            pointData[2]
        };
        pointData += stride / sizeof(float);
    }
    JPH::ConvexHullShapeSettings shape_settings{points};
	auto result = shape_settings.Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}

} // namespace erhe::physics
