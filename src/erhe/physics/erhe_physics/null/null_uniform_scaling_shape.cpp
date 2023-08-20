#include "erhe_physics/null/null_uniform_scaling_shape.hpp"

namespace erhe::physics
{

auto ICollision_shape::create_uniform_scaling_shape(ICollision_shape* shape, const float scale) -> ICollision_shape*
{
    return new Null_uniform_scaling_shape(shape, scale);
}

auto ICollision_shape::create_uniform_scaling_shape_shared(ICollision_shape* shape, const float scale) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Null_uniform_scaling_shape>(shape, scale);
}

void Null_uniform_scaling_shape::calculate_local_inertia(const float mass, glm::mat4& inertia) const
{
    static_cast<void>(mass);
    inertia = glm::mat4{1.0f};
}

auto Null_uniform_scaling_shape::is_convex() const -> bool
{
    return true;
}

auto Null_uniform_scaling_shape::get_center_of_mass() const -> glm::vec3
{
    return glm::vec3{0.0f};
}

auto Null_uniform_scaling_shape::get_mass_properties() const -> Mass_properties
{
    return Mass_properties{
        .mass           = 0.0f,
        .inertia_tensor = glm::mat4{1.0f}
    };
}

} // namespace erhe::physics
