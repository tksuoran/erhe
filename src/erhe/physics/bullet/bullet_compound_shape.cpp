#include "erhe/physics/bullet/bullet_compound_shape.hpp"
#include "erhe/physics/bullet/glm_conversions.hpp"
#include "erhe/toolkit/verify.hpp"

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

Bullet_compound_shape::~Bullet_compound_shape()
{
}

void Bullet_compound_shape::add_child_shape(
    const std::shared_ptr<ICollision_shape>& shape,
    const Transform                          transform
)
{
    auto bullet_collision_shape = dynamic_pointer_cast<Bullet_collision_shape>(shape);
    ERHE_VERIFY(bullet_collision_shape);
    m_children.push_back(bullet_collision_shape);
    m_compound_shape.addChildShape(
        to_bullet(transform),
        bullet_collision_shape->get_bullet_collision_shape()
    );
}

auto Bullet_compound_shape::is_convex() const -> bool
{
    return false;
}

void Bullet_compound_shape::calculate_principal_axis_transform(
    const std::vector<float>& child_masses,
    Transform&                principal_transform,
    glm::vec3&                inertia)
{
	/// computes
    /// - the exact moment of inertia and
    /// - the transform
    /// from
    /// - the coordinate system defined by the principal axes of the moment of inertia and
    /// - the center of mass to the current coordinate system.
    ///
    /// "masses" points to an array of masses of the children.
    ///
    /// The resulting transform "principal" has to be applied inversely to all children transforms
    /// in order for the local coordinate system of the compound shape
    /// to be centered at the center of mass and
    /// to coincide with the principal axes.
    ///
    /// This also necessitates a correction of the world transform
	/// of the collision object by the principal transform.
    const int child_count = m_compound_shape.getNumChildShapes();
    ERHE_VERIFY(child_masses.size() == child_count);

    btTransform bullet_principal_transform;
    btVector3 bullet_inertia;
    m_compound_shape.calculatePrincipalAxisTransform(
        child_masses.data(),
        bullet_principal_transform,
        bullet_inertia
    );

    btTransform inverse_principal_transform = bullet_principal_transform.inverse();

    for (
        int child_index = 0;
        child_index < child_count;
        ++child_index
    )
    {
        btTransform child_transform     = m_compound_shape.getChildTransform(child_index);
        btTransform new_child_transform = inverse_principal_transform * child_transform;
        m_compound_shape.updateChildTransform(child_index, new_child_transform);
    }

    principal_transform = from_bullet(bullet_principal_transform);
    inertia             = from_bullet(bullet_inertia);
}


} // namespace erhe::physics
