#include "erhe/physics/bullet/bullet_collision_shape.hpp"
#include "erhe/physics/bullet/glm_conversions.hpp"
#include "erhe/physics/physics_log.hpp"

namespace erhe::physics
{

auto ICollision_shape::create_box_shape(
    const glm::vec3 half_extents
) -> ICollision_shape*
{
    return new Bullet_box_shape(half_extents);
}

auto ICollision_shape::create_box_shape_shared(
    const glm::vec3 half_extents
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Bullet_box_shape>(half_extents);
}

auto ICollision_shape::create_capsule_shape(
    const Axis  axis,
    const float radius,
    const float length
) -> ICollision_shape*
{
    switch (axis)
    {
        //using enum Axis;
        case Axis::X: return new Bullet_capsule_x_shape(radius, length);
        case Axis::Y: return new Bullet_capsule_y_shape(radius, length);
        case Axis::Z: return new Bullet_capsule_z_shape(radius, length);
        default:      return nullptr;
    }
}

auto ICollision_shape::create_capsule_shape_shared(
    const Axis  axis,
    const float radius,
    const float length
) -> std::shared_ptr<ICollision_shape>
{
    switch (axis)
    {
        //using enum Axis;
        case Axis::X: return std::make_shared<Bullet_capsule_x_shape>(radius, length);
        case Axis::Y: return std::make_shared<Bullet_capsule_y_shape>(radius, length);
        case Axis::Z: return std::make_shared<Bullet_capsule_z_shape>(radius, length);
        default:      return {};
    }
}

//auto ICollision_shape::create_cone_shape(
//    const Axis  axis,
//    const float base_radius,
//    const float height
//) -> ICollision_shape*
//{
//    switch (axis)
//    {
//        //using enum Axis;
//        case Axis::X: return new Bullet_cone_x_shape(base_radius, height);
//        case Axis::Y: return new Bullet_cone_y_shape(base_radius, height);
//        case Axis::Z: return new Bullet_cone_z_shape(base_radius, height);
//        default:      return nullptr;
//    }
//}
//
//auto ICollision_shape::create_cone_shape_shared(
//    const Axis  axis,
//    const float base_radius,
//    const float height
//) -> std::shared_ptr<ICollision_shape>
//{
//    switch (axis)
//    {
//        //using enum Axis;
//        case Axis::X: return std::make_shared<Bullet_cone_x_shape>(base_radius, height);
//        case Axis::Y: return std::make_shared<Bullet_cone_y_shape>(base_radius, height);
//        case Axis::Z: return std::make_shared<Bullet_cone_z_shape>(base_radius, height);
//        default:      return {};
//    }
//}

auto ICollision_shape::create_cylinder_shape(
    const Axis      axis,
    const glm::vec3 half_extents
) -> ICollision_shape*
{
    switch (axis)
    {
        //using enum Axis;
        case Axis::X: return new Bullet_cylinder_x_shape(half_extents);
        case Axis::Y: return new Bullet_cylinder_y_shape(half_extents);
        case Axis::Z: return new Bullet_cylinder_z_shape(half_extents);
        default:      return nullptr;
    }
}

auto ICollision_shape::create_cylinder_shape_shared(
    const Axis      axis,
    const glm::vec3 half_extents
) -> std::shared_ptr<ICollision_shape>
{
    switch (axis)
    {
        //using enum Axis;
        case Axis::X: return std::make_shared<Bullet_cylinder_x_shape>(half_extents);
        case Axis::Y: return std::make_shared<Bullet_cylinder_y_shape>(half_extents);
        case Axis::Z: return std::make_shared<Bullet_cylinder_z_shape>(half_extents);
        default:      return nullptr;
    }
}

auto ICollision_shape::create_sphere_shape(
    const float radius
) -> ICollision_shape*
{
    return new Bullet_sphere_shape(radius);
}

auto ICollision_shape::create_sphere_shape_shared(
    const float radius
) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Bullet_sphere_shape>(radius);
}

Bullet_collision_shape::Bullet_collision_shape(btCollisionShape* shape)
    : m_bullet_collision_shape{shape}
{
}

Bullet_collision_shape::~Bullet_collision_shape() noexcept
{
}

void Bullet_collision_shape::calculate_local_inertia(float mass, glm::mat4& inertia) const
{
    btVector3 bulletInertia{0.0f, 0.0f, 0.0f};
    m_bullet_collision_shape->calculateLocalInertia(static_cast<btScalar>(mass), bulletInertia);
    const glm::vec3 i = from_bullet(bulletInertia);
    inertia = glm::mat4{
        i.x, 0.0f, 0.0f, 0.0f,
        0.0f, i.y, 0.0f, 0.0f,
        0.0f, 0.0f, i.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
}

auto Bullet_collision_shape::is_convex() const -> bool
{
    return true;
}

void Bullet_collision_shape::calculate_principal_axis_transform(
    const std::vector<float>& /*child_masses*/,
    Transform&                /*principal_transform*/,
    glm::vec3&                /*inertia*/
)
{
    log_physics->error("Bullet_collision_shape::calculate_principal_axis_transform() called");
    assert(false);
}

Bullet_box_shape::Bullet_box_shape(const glm::vec3 half_extents)
    : Bullet_collision_shape{&m_box_shape}
    , m_box_shape           {to_bullet(half_extents)}
{
}

Bullet_capsule_x_shape::Bullet_capsule_x_shape(const float radius, const float length)
    : Bullet_collision_shape{&m_capsule_shape}
    , m_capsule_shape       {static_cast<btScalar>(radius), static_cast<btScalar>(length)}
{
}

Bullet_capsule_y_shape::Bullet_capsule_y_shape(const float radius, const float length)
    : Bullet_collision_shape{&m_capsule_shape}
    , m_capsule_shape       {static_cast<btScalar>(radius), static_cast<btScalar>(length)}
{
}

Bullet_capsule_z_shape::Bullet_capsule_z_shape(const float radius, const float length)
    : Bullet_collision_shape{&m_capsule_shape}
    , m_capsule_shape       {static_cast<btScalar>(radius), static_cast<btScalar>(length)}
{
}

Bullet_cone_x_shape::Bullet_cone_x_shape(const float base_radius, const float height)
    : Bullet_collision_shape{&m_cone_shape}
    , m_cone_shape          {static_cast<btScalar>(base_radius), static_cast<btScalar>(height)}
{
}

Bullet_cone_y_shape::Bullet_cone_y_shape(const float base_radius, const float height)
    : Bullet_collision_shape{&m_cone_shape}
    , m_cone_shape          {static_cast<btScalar>(base_radius), static_cast<btScalar>(height)}
{
}

Bullet_cone_z_shape::Bullet_cone_z_shape(const float base_radius, const float height)
    : Bullet_collision_shape{&m_cone_shape}
    , m_cone_shape          {static_cast<btScalar>(base_radius), static_cast<btScalar>(height)}
{
}

Bullet_cylinder_x_shape::Bullet_cylinder_x_shape(const glm::vec3 half_extents)
    : Bullet_collision_shape{&m_cylinder_shape}
    , m_cylinder_shape      {to_bullet(half_extents)}
{
}

Bullet_cylinder_y_shape::Bullet_cylinder_y_shape(const glm::vec3 half_extents)
    : Bullet_collision_shape{&m_cylinder_shape}
    , m_cylinder_shape      {to_bullet(half_extents)}
{
}

Bullet_cylinder_z_shape::Bullet_cylinder_z_shape(const glm::vec3 half_extents)
    : Bullet_collision_shape{&m_cylinder_shape}
    , m_cylinder_shape      {to_bullet(half_extents)}
{
}

Bullet_sphere_shape::Bullet_sphere_shape(const float radius)
    : Bullet_collision_shape{&m_sphere_shape}
    , m_sphere_shape        {static_cast<btScalar>(radius)}
{
}


} // namespace erhe::physics
