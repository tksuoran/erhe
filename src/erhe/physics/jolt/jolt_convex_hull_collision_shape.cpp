#include "erhe/physics/jolt/jolt_convex_hull_collision_shape.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>

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

namespace {

auto make_convex_hull_shape_settings(
    const float* const point_data,
    const int          point_count,
    const int          stride
) -> JPH::ConvexHullShapeSettings
{
    std::vector<JPH::Vec3> points;
    points.reserve(point_count);
    const float* data = point_data;
    for (int i = 0; i < point_count; ++i)
    {
        const float x = data[0];
        const float y = data[1];
        const float z = data[2];
        points.emplace_back(x, y, z);
        data += stride / sizeof(float);
    }
    return JPH::ConvexHullShapeSettings{points};
}

}

Jolt_convex_hull_collision_shape::Jolt_convex_hull_collision_shape(
    const float* point_data,
    const int    point_count,
    const int    stride
)
    : m_shape_settings{
        make_convex_hull_shape_settings(
            point_data, point_count, stride
        )
    }
{
	auto result = m_shape_settings.Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}

//    std::vector<JPH::Vec3> points;
//    points.resize(numPoints);
//    for (int i = 0; i < numPoints; ++i)
//    {
//        points[i] = JPH::Vec3{
//            pointData[0],
//            pointData[1],
//            pointData[2]
//        };
//        pointData += stride / sizeof(float);
//    }
//    JPH::ConvexHullShapeSettings shape_settings{points};
//	auto result = shape_settings.Create();
//    ERHE_VERIFY(result.IsValid());
//    m_jolt_shape = result.Get();
//}

} // namespace erhe::physics
