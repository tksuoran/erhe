#pragma once

#include "erhe/physics/icollision_shape.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>

namespace erhe::physics
{

class Null_collision_shape
    : public ICollision_shape
{
public:
    // Implements ICollision_shape
    void calculate_local_inertia(float mass, glm::vec3& inertia) const override
    {
        inertia = glm::vec3{0.0f};
    }

    auto is_convex() const -> bool override
    {
        return true;
    }

    void add_child_shape(
        std::shared_ptr<ICollision_shape>,
        const glm::mat3,
        const glm::vec3
    ) override
    {
        assert(false);
    }
};

class Null_box_shape
    : public Null_collision_shape
{
public:
    explicit Null_box_shape(const glm::vec3 half_extents)
        : m_half_extents{half_extents}
    {
    }

private:
    const glm::vec3 m_half_extents;
};

class Null_capsule_shape
    : public Null_collision_shape
{
public:
    Null_capsule_shape(const Axis axis, const float radius, const float length)
        : m_axis{axis}
        , m_radius{radius}
        , m_length{length}
    {
    }

private:
    Axis  m_axis;
    float m_radius;
    float m_length;
};


class Null_cone_shape
    : public Null_collision_shape
{
public:
    Null_cone_shape(const Axis axis, const float base_radius, const float height)
        : m_axis{axis}
        , m_base_radius{base_radius}
        , m_height{height}
    {
    }

private:
    Axis  m_axis;
    float m_base_radius;
    float m_height;
};

class Null_cylinder_shape
    : public Null_collision_shape
{
public:
    Null_cylinder_shape(const Axis axis, const glm::vec3 half_extents)
        : m_axis{axis}
        , m_half_extents{half_extents}
    {
    }

private:
    Axis m_axis;
    glm::vec3 m_half_extents;
};


class Null_sphere_shape
    : public Null_collision_shape
{
public:
    Null_sphere_shape(const float radius)
        : m_radius{radius}
    {
    }

private:
    float m_radius;
};

} // namespace erhe::physics
