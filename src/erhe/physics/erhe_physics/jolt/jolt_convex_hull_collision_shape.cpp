#include "erhe_physics/jolt/jolt_convex_hull_collision_shape.hpp"
#include "erhe_physics/physics_log.hpp"

namespace erhe::physics {

namespace {

auto make_convex_hull_shape_settings(const float* const point_data, const int point_count, const int stride) -> JPH::Ref<JPH::ConvexHullShapeSettings>
{
    JPH::Array<JPH::Vec3> points;
    points.reserve(point_count);
    const float* data = point_data;
    for (int i = 0; i < point_count; ++i) {
        points.emplace_back(data[0], data[1], data[2]);
        data += stride / sizeof(float);
    }
    return new JPH::ConvexHullShapeSettings{points};
}

// Null when Jolt refuses the points (a hull thinner than its tolerance, too few points), logged.
auto make_jolt_convex_hull_shape(const float* const point_data, const int point_count, const int stride) -> std::shared_ptr<Jolt_convex_hull_collision_shape>
{
    JPH::Ref<JPH::ConvexHullShapeSettings> settings = make_convex_hull_shape_settings(point_data, point_count, stride);
    const JPH::ShapeSettings::ShapeResult result = settings->Create();
    if (!result.IsValid()) {
        log_physics->warn("Convex hull of {} points: Jolt refuses it: {}", point_count, result.GetError().c_str());
        return {};
    }
    return std::make_shared<Jolt_convex_hull_collision_shape>(settings, result.Get());
}

}

auto ICollision_shape::create_convex_hull_shape(const float* points, const int numPoints, const int stride) -> ICollision_shape*
{
    JPH::Ref<JPH::ConvexHullShapeSettings> settings = make_convex_hull_shape_settings(points, numPoints, stride);
    const JPH::ShapeSettings::ShapeResult result = settings->Create();
    if (!result.IsValid()) {
        log_physics->warn("Convex hull of {} points: Jolt refuses it: {}", numPoints, result.GetError().c_str());
        return nullptr;
    }
    return new Jolt_convex_hull_collision_shape(settings, result.Get());
}

auto ICollision_shape::create_convex_hull_shape_shared(const float* points, const int numPoints, const int stride) -> std::shared_ptr<ICollision_shape>
{
    return make_jolt_convex_hull_shape(points, numPoints, stride);
}

Jolt_convex_hull_collision_shape::Jolt_convex_hull_collision_shape(JPH::Ref<JPH::ConvexHullShapeSettings> settings, JPH::RefConst<JPH::Shape> shape)
    : m_shape_settings{std::move(settings)}
{
    m_jolt_shape = std::move(shape);
}

auto Jolt_convex_hull_collision_shape::get_shape_settings() -> JPH::ShapeSettings&
{
    return *m_shape_settings.GetPtr();
}

auto Jolt_convex_hull_collision_shape::describe() const -> std::string
{
    return "Jolt_convex_hull_collision_shape";
}

auto Jolt_convex_hull_collision_shape::get_shape_type() const -> Collision_shape_type
{
    return Collision_shape_type::e_convex_hull;
}

//    std::vector<JPH::Vec3> points;
//    points.resize(numPoints);
//    for (int i = 0; i < numPoints; ++i) {
//        points[i] = JPH::Vec3{
//            pointData[0],
//            pointData[1],
//            pointData[2]
//        };
//        pointData += stride / sizeof(float);
//    }
//    JPH::ConvexHullShapeSettings shape_settings{points};
//    auto result = shape_settings.Create();
//    ERHE_VERIFY(result.IsValid());
//    m_jolt_shape = result.Get();
//}

} // namespace erhe::physics
