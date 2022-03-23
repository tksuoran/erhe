#include "erhe/physics/jolt/jolt_collision_shape.hpp"

namespace erhe::physics
{

auto ICollision_shape::create_box_shape(const glm::vec3 half_extents)
    -> ICollision_shape*
{
    return new Jolt_box_shape(half_extents);
}

auto ICollision_shape::create_box_shape_shared(const glm::vec3 half_extents)
    -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_box_shape>(half_extents);
}

auto ICollision_shape::create_capsule_shape(
    const Axis  axis,
    const float radius,
    const float length
) -> ICollision_shape*
{
    return new Jolt_capsule_shape(axis, radius, length);
}

auto ICollision_shape::create_capsule_shape_shared(
    const Axis  axis,
    const float radius,
    const float length
) -> std::shared_ptr<ICollision_shape>
{
     return std::make_shared<Jolt_capsule_shape>(axis, radius, length);
}

#if 0
auto ICollision_shape::create_cone_shape(
    const Axis  axis,
    const float base_radius,
    const float height
) -> ICollision_shape*
{
    return new Jolt_cone_shape(axis, base_radius, height);
}

auto ICollision_shape::create_cone_shape_shared(
    const Axis  axis,
    const float base_radius,
    const float height
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_cone_shape>(axis, base_radius, height);
}
#endif

auto ICollision_shape::create_cylinder_shape(
    const Axis      axis,
    const glm::vec3 half_extents
) -> ICollision_shape*
{
    return new Jolt_cylinder_shape(axis, half_extents);
}

auto ICollision_shape::create_cylinder_shape_shared(
    const Axis      axis,
    const glm::vec3 half_extents
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_cylinder_shape>(axis, half_extents);
}

auto ICollision_shape::create_sphere_shape(const float radius)
    -> ICollision_shape*
{
    return new Jolt_sphere_shape(radius);
}

auto ICollision_shape::create_sphere_shape_shared(const float radius)
    -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_sphere_shape>(radius);
}

} // namespace erhe::physics
