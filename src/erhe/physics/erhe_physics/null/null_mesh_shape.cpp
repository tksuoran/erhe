#include "erhe_physics/null/null_mesh_shape.hpp"

namespace erhe::physics {

auto ICollision_shape::create_mesh_shape(
    const float*    points,
    const int       point_count,
    const int       point_stride_bytes,
    const uint32_t* indices,
    const int       triangle_count
) -> ICollision_shape*
{
    return new Null_mesh_shape(points, point_count, point_stride_bytes, indices, triangle_count);
}

auto ICollision_shape::create_mesh_shape_shared(
    const float*    points,
    const int       point_count,
    const int       point_stride_bytes,
    const uint32_t* indices,
    const int       triangle_count
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Null_mesh_shape>(points, point_count, point_stride_bytes, indices, triangle_count);
}

Null_mesh_shape::Null_mesh_shape(
    const float*    points,
    const int       point_count,
    const int       point_stride_bytes,
    const uint32_t* indices,
    const int       triangle_count
)
{
    static_cast<void>(points);
    static_cast<void>(point_count);
    static_cast<void>(point_stride_bytes);
    static_cast<void>(indices);
    static_cast<void>(triangle_count);
}

} // namespace erhe::physics
