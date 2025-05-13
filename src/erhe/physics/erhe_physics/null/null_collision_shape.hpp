#pragma once

#include "erhe_physics/icollision_shape.hpp"

#include <glm/glm.hpp>

namespace erhe::physics {

class Null_collision_shape
    : public ICollision_shape
{
public:
    ~Null_collision_shape() noexcept override = default;

    void calculate_local_inertia(float mass, glm::mat4& inertia) const override;
    auto is_convex              () const -> bool                       override;
    auto get_center_of_mass     () const -> glm::vec3                  override;
    auto get_mass_properties    () const -> Mass_properties            override;
    auto describe               () const -> std::string                override;
};

class Null_box_shape
    : public Null_collision_shape
{
public:
    ~Null_box_shape() noexcept override = default;

    explicit Null_box_shape(const glm::vec3)
    {
    }
};

class Null_capsule_shape
    : public Null_collision_shape
{
public:
    Null_capsule_shape(const Axis, const float, const float)
    {
    }

    ~Null_capsule_shape() noexcept override = default;
};


class Null_cone_shape
    : public Null_collision_shape
{
public:
    Null_cone_shape(const Axis, const float, const float)
    {
    }

    ~Null_cone_shape() noexcept override = default;
};

class Null_cylinder_shape
    : public Null_collision_shape
{
public:
    Null_cylinder_shape(const Axis, const glm::vec3)
    {
    }

    ~Null_cylinder_shape() noexcept override = default;
};


class Null_sphere_shape
    : public Null_collision_shape
{
public:
    explicit Null_sphere_shape(const float)
    {
    }

    ~Null_sphere_shape() noexcept override = default;
};

} // namespace erhe::physics
