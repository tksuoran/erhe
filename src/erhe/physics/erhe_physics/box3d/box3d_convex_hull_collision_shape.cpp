#include "erhe_physics/box3d/box3d_convex_hull_collision_shape.hpp"

#include <fmt/format.h>

namespace erhe::physics {

Box3d_convex_hull_collision_shape::Box3d_convex_hull_collision_shape(
    const float* points,
    const int    point_count,
    const int    stride
)
    : m_source_point_count{point_count}
    , m_hull              {create_convex_hull(points, point_count, stride)}
{
}

void Box3d_convex_hull_collision_shape::attach_to_body(
    Shape_attach_context& context,
    const b3Transform&    local_transform,
    const glm::vec3&      scale
) const
{
    attach_hull_to_body(context, m_hull.get(), local_transform, scale, "convex hull");
}

auto Box3d_convex_hull_collision_shape::compute_mass_data(const float density) const -> b3MassData
{
    if (!m_hull.is_valid()) {
        return Box3d_collision_shape::compute_mass_data(density);
    }
    return b3ComputeHullMass(m_hull.get(), density);
}

auto Box3d_convex_hull_collision_shape::describe() const -> std::string
{
    // Box3D reduces the input point cloud to at most B3_MAX_HULL_VERTICES, so
    // the resulting vertex count is worth reporting alongside the input size.
    return fmt::format(
        "Box3d_convex_hull_collision_shape(source points = {}, hull vertices = {})",
        m_source_point_count,
        m_hull.is_valid() ? m_hull.get()->vertexCount : 0
    );
}

auto ICollision_shape::create_convex_hull_shape(
    const float* points,
    const int    point_count,
    const int    stride
) -> ICollision_shape*
{
    return new Box3d_convex_hull_collision_shape(points, point_count, stride);
}

auto ICollision_shape::create_convex_hull_shape_shared(
    const float* points,
    const int    point_count,
    const int    stride
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Box3d_convex_hull_collision_shape>(points, point_count, stride);
}

} // namespace erhe::physics
