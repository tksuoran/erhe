#pragma once

#include "erhe/physics/icollision_shape.hpp"
#include "erhe/toolkit/optional.hpp"

#include <btBulletCollisionCommon.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btConeShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class Bullet_collision_shape
    : public ICollision_shape
{
public:
    explicit Bullet_collision_shape(btCollisionShape* shape);
    ~Bullet_collision_shape() noexcept override;

    // Implements ICollision_shape
    [[nodiscard]] auto is_convex() const -> bool                       override;
    void calculate_local_inertia(float mass, glm::vec3& inertia) const override;

    void calculate_principal_axis_transform(
        const std::vector<float>& child_masses,
        Transform&                out_principal_transform,
        glm::vec3&                inertia
    ) override;

    [[nodiscard]] auto get_bullet_collision_shape() -> btCollisionShape*
    {
        return m_bullet_collision_shape;
    }

    [[nodiscard]] auto get_bullet_collision_shape() const -> const btCollisionShape*
    {
        return m_bullet_collision_shape;
    }

protected:
    btCollisionShape* m_bullet_collision_shape{nullptr};
};

class Bullet_box_shape
    : public Bullet_collision_shape
{
public:
    explicit Bullet_box_shape(const glm::vec3 half_extents);

private:
    btBoxShape m_box_shape;
};

class Bullet_capsule_x_shape
    : public Bullet_collision_shape
{
public:
    Bullet_capsule_x_shape(const float radius, const float length);

private:
    btCapsuleShapeX m_capsule_shape;
};

class Bullet_capsule_y_shape
    : public Bullet_collision_shape
{
public:
    Bullet_capsule_y_shape(const float radius, const float length);

private:
    btCapsuleShape m_capsule_shape;
};

class Bullet_capsule_z_shape
    : public Bullet_collision_shape
{
public:
    Bullet_capsule_z_shape(const float radius, const float length);

private:
    btCapsuleShapeZ m_capsule_shape;
};

class Bullet_cone_x_shape
    : public Bullet_collision_shape
{
public:
    Bullet_cone_x_shape(const float base_radius, const float height);

private:
    btConeShapeX m_cone_shape;
};

class Bullet_cone_y_shape
    : public Bullet_collision_shape
{
public:
    Bullet_cone_y_shape(const float base_radius, const float height);

private:
    btConeShape m_cone_shape;
};

class Bullet_cone_z_shape
    : public Bullet_collision_shape
{
public:
    Bullet_cone_z_shape(const float base_radius, const float height);

private:
    btConeShapeZ m_cone_shape;
};

class Bullet_cylinder_x_shape
    : public Bullet_collision_shape
{
public:
    explicit Bullet_cylinder_x_shape(const glm::vec3 half_extents);

private:
    btCylinderShapeX m_cylinder_shape;
};

class Bullet_cylinder_y_shape
    : public Bullet_collision_shape
{
public:
    explicit Bullet_cylinder_y_shape(const glm::vec3 half_extents);

private:
    btCylinderShape m_cylinder_shape;
};

class Bullet_cylinder_z_shape
    : public Bullet_collision_shape
{
public:
    explicit Bullet_cylinder_z_shape(const glm::vec3 half_extents);

private:
    btCylinderShapeZ m_cylinder_shape;
};

class Bullet_sphere_shape
    : public Bullet_collision_shape
{
public:
    explicit Bullet_sphere_shape(const float radius);

private:
    btSphereShape m_sphere_shape;
};

} // namespace erhe::physics
