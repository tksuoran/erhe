#include "erhe_physics/jolt/jolt_mesh_shape.hpp"
#include "erhe_verify/verify.hpp"

#include <utility>

namespace erhe::physics {

auto ICollision_shape::create_mesh_shape(
    const float*    points,
    const int       point_count,
    const int       point_stride_bytes,
    const uint32_t* indices,
    const int       triangle_count
) -> ICollision_shape*
{
    return new Jolt_mesh_shape(points, point_count, point_stride_bytes, indices, triangle_count);
}

auto ICollision_shape::create_mesh_shape_shared(
    const float*    points,
    const int       point_count,
    const int       point_stride_bytes,
    const uint32_t* indices,
    const int       triangle_count
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_mesh_shape>(points, point_count, point_stride_bytes, indices, triangle_count);
}

Jolt_mesh_shape::Jolt_mesh_shape(
    const float* const    points,
    const int             point_count,
    const int             point_stride_bytes,
    const uint32_t* const indices,
    const int             triangle_count
)
{
    JPH::VertexList vertices;
    vertices.reserve(point_count);
    const float* point_data = points;
    for (int i = 0; i < point_count; ++i) {
        vertices.emplace_back(point_data[0], point_data[1], point_data[2]);
        point_data += point_stride_bytes / sizeof(float);
    }
    JPH::IndexedTriangleList triangles;
    triangles.reserve(triangle_count);
    for (int i = 0; i < triangle_count; ++i) {
        triangles.emplace_back(
            indices[(i * 3) + 0],
            indices[(i * 3) + 1],
            indices[(i * 3) + 2]
        );
    }
    m_shape_settings = new JPH::MeshShapeSettings(std::move(vertices), std::move(triangles));
    auto result = m_shape_settings->Create();
    ERHE_VERIFY(result.IsValid());
    m_jolt_shape = result.Get();
}

auto Jolt_mesh_shape::is_convex() const -> bool
{
    return false;
}

auto Jolt_mesh_shape::get_shape_settings() -> JPH::ShapeSettings&
{
    return *m_shape_settings.GetPtr();
}

auto Jolt_mesh_shape::describe() const -> std::string
{
    return "Jolt_mesh_shape";
}

auto Jolt_mesh_shape::get_shape_type() const -> Collision_shape_type
{
    return Collision_shape_type::e_mesh;
}

} // namespace erhe::physics
