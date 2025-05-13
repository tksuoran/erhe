#include "erhe_physics/null/null_collision_shape.hpp"

namespace erhe::physics {

auto ICollision_shape::create_empty_shape() -> ICollision_shape*
{
    return new Null_box_shape(glm::vec3{0.0f});
}

auto ICollision_shape::create_empty_shape_shared() -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Null_box_shape>(glm::vec3{0.0f});
}

auto ICollision_shape::create_box_shape(const glm::vec3 half_extents) -> ICollision_shape*
{
    return new Null_box_shape(half_extents);
}

auto ICollision_shape::create_box_shape_shared(const glm::vec3 half_extents) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Null_box_shape>(half_extents);
}

auto ICollision_shape::create_capsule_shape(
    const Axis  axis,
    const float radius,
    const float length
) -> ICollision_shape*
{
    return new Null_capsule_shape(axis, radius, length);
}

auto ICollision_shape::create_capsule_shape_shared(
    const Axis  axis,
    const float radius,
    const float length
) -> std::shared_ptr<ICollision_shape>
{
     return std::make_shared<Null_capsule_shape>(axis, radius, length);
}

//auto ICollision_shape::create_cone_shape(
//    const Axis  axis,
//    const float base_radius,
//    const float height
//) -> ICollision_shape*
//{
//    return new Null_cone_shape(axis, base_radius, height);
//}

//auto ICollision_shape::create_cone_shape_shared(
//    const Axis  axis,
//    const float base_radius,
//    const float height
//) -> std::shared_ptr<ICollision_shape>
//{
//    return std::make_shared<Null_cone_shape>(axis, base_radius, height);
//}

auto ICollision_shape::create_cylinder_shape(
    const Axis      axis,
    const glm::vec3 half_extents
) -> ICollision_shape*
{
    return new Null_cylinder_shape(axis, half_extents);
}

auto ICollision_shape::create_cylinder_shape_shared(
    const Axis      axis,
    const glm::vec3 half_extents
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Null_cylinder_shape>(axis, half_extents);
}

auto ICollision_shape::create_sphere_shape(const float radius)
    -> ICollision_shape*
{
    return new Null_sphere_shape(radius);
}

auto ICollision_shape::create_sphere_shape_shared(const float radius)
    -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Null_sphere_shape>(radius);
}

void Null_collision_shape::calculate_local_inertia(const float mass, glm::mat4& inertia) const
{
    static_cast<void>(mass);
    inertia = glm::mat4{1.0f};
}

auto Null_collision_shape::is_convex() const -> bool
{
    return true;
}

auto Null_collision_shape::get_center_of_mass() const -> glm::vec3
{
    return glm::vec3{0.0f};
}

auto Null_collision_shape::get_mass_properties() const -> Mass_properties
{
    return Mass_properties
    {
        .mass           = 1.0f,
        .inertia_tensor = glm::mat4{1.0f}
    };
}

auto Null_collision_shape::describe() const -> std::string
{
    return {};
}

} // namespace erhe::physics
