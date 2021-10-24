#include "erhe/physics/bullet_compound_shape.hpp"
#include "erhe/physics/glm_conversions.hpp"

namespace erhe::physics
{

auto ICollision_shape::create_compound_shape() -> ICollision_shape*
{
    return new Bullet_compound_shape();
}

auto ICollision_shape::create_compound_shape_shared() -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Bullet_compound_shape>();
}


Bullet_compound_shape::Bullet_compound_shape()
    : Bullet_collision_shape{&m_compound_shape}
{
}

void Bullet_compound_shape::add_child_shape(
    ICollision_shape* shape,
    const glm::mat3   basis,
    const glm::vec3   origin
)
{
    Bullet_collision_shape* bullet_collision_shape = static_cast<Bullet_collision_shape*>(shape);
    const btTransform transform{from_glm(basis), from_glm(origin)};
    m_compound_shape.addChildShape(transform, bullet_collision_shape->get_bullet_collision_shape());
}

auto Bullet_compound_shape::is_convex() const -> bool
{
    return false;
}

} // namespace erhe::physics
