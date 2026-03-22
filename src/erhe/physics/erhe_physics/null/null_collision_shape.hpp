#pragma once

#include "erhe_physics/icollision_shape.hpp"

#include <glm/glm.hpp>

namespace erhe::physics {

class Null_collision_shape  : public ICollision_shape
{
public:
    ~Null_collision_shape() noexcept override = default;

    void calculate_local_inertia(float mass, glm::mat4& inertia) const override;
    auto is_convex              () const -> bool                       override;
    auto get_center_of_mass     () const -> glm::vec3                  override;
    auto get_mass_properties    () const -> Mass_properties            override;
    auto describe               () const -> std::string                override;
    auto get_shape_type         () const -> Collision_shape_type       override;
};

class Null_box_shape : public Null_collision_shape
{
public:
    ~Null_box_shape() noexcept override = default;

    explicit Null_box_shape(const glm::vec3 half_extents)
        : m_half_extents{half_extents}
    {
    }

    [[nodiscard]] auto get_shape_type  () const -> Collision_shape_type      override { return Collision_shape_type::e_box; }
    [[nodiscard]] auto get_half_extents() const -> std::optional<glm::vec3>  override { return m_half_extents; }

private:
    glm::vec3 m_half_extents;
};

class Null_capsule_shape : public Null_collision_shape
{
public:
    Null_capsule_shape(const Axis axis, const float radius, const float length)
        : m_axis{axis}, m_radius{radius}, m_length{length}
    {
    }

    ~Null_capsule_shape() noexcept override = default;

    [[nodiscard]] auto get_shape_type() const -> Collision_shape_type   override { return Collision_shape_type::e_capsule; }
    [[nodiscard]] auto get_radius    () const -> std::optional<float>   override { return m_radius; }
    [[nodiscard]] auto get_axis      () const -> std::optional<Axis>    override { return m_axis; }
    [[nodiscard]] auto get_length    () const -> std::optional<float>   override { return m_length; }

private:
    Axis  m_axis;
    float m_radius;
    float m_length;
};


class Null_cone_shape : public Null_collision_shape
{
public:
    Null_cone_shape(const Axis, const float, const float)
    {
    }

    ~Null_cone_shape() noexcept override = default;
};

class Null_cylinder_shape : public Null_collision_shape
{
public:
    Null_cylinder_shape(const Axis axis, const glm::vec3 half_extents)
        : m_axis{axis}, m_half_extents{half_extents}
    {
    }

    ~Null_cylinder_shape() noexcept override = default;

    [[nodiscard]] auto get_shape_type  () const -> Collision_shape_type      override { return Collision_shape_type::e_cylinder; }
    [[nodiscard]] auto get_half_extents() const -> std::optional<glm::vec3>  override { return m_half_extents; }
    [[nodiscard]] auto get_axis        () const -> std::optional<Axis>       override { return m_axis; }

private:
    Axis      m_axis;
    glm::vec3 m_half_extents;
};


class Null_sphere_shape : public Null_collision_shape
{
public:
    explicit Null_sphere_shape(const float radius)
        : m_radius{radius}
    {
    }

    ~Null_sphere_shape() noexcept override = default;

    [[nodiscard]] auto get_shape_type() const -> Collision_shape_type  override { return Collision_shape_type::e_sphere; }
    [[nodiscard]] auto get_radius    () const -> std::optional<float>  override { return m_radius; }

private:
    float m_radius;
};

} // namespace erhe::physics
