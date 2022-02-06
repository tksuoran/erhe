#pragma once

#include "erhe/physics/icollision_shape.hpp"
#include "erhe/toolkit/optional.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class Null_collision_shape
    : public ICollision_shape
{
public:
    ~Null_collision_shape() override = default;

    // Implements ICollision_shape
    void calculate_local_inertia(float mass, glm::vec3& inertia) const override
    {
        static_cast<void>(mass);
        inertia = glm::vec3{0.0f};
    }

    auto is_convex() const -> bool override
    {
        return true;
    }

    void add_child_shape(
        const std::shared_ptr<ICollision_shape>&,
        const Transform
    ) override
    {
        //assert(false);
    }

    virtual void calculate_principal_axis_transform(
        const std::vector<float>& /*child_masses*/,
        Transform&                /*principal_transform*/,
        glm::vec3&                /*inertia*/
    ) override
    {
        //assert(false);
    }

};

class Null_box_shape
    : public Null_collision_shape
{
public:
    ~Null_box_shape() override = default;

    explicit Null_box_shape(const glm::vec3 half_extents)
        //: m_half_extents{half_extents}
    {
        static_cast<void>(half_extents);
    }

private:
    //const glm::vec3 m_half_extents;
};

class Null_capsule_shape
    : public Null_collision_shape
{
public:
    Null_capsule_shape(const Axis axis, const float radius, const float length)
        //: m_axis{axis}
        //, m_radius{radius}
        //, m_length{length}
    {
        static_cast<void>(axis);
        static_cast<void>(radius);
        static_cast<void>(length);
    }

    ~Null_capsule_shape() override = default;

private:
    //Axis  m_axis;
    //float m_radius;
    //float m_length;
};


class Null_cone_shape
    : public Null_collision_shape
{
public:
    Null_cone_shape(const Axis axis, const float base_radius, const float height)
        //: m_axis{axis}
        //, m_base_radius{base_radius}
        //, m_height{height}
    {
        static_cast<void>(axis);
        static_cast<void>(base_radius);
        static_cast<void>(height);
    }

    ~Null_cone_shape() override = default;

private:
    //Axis  m_axis;
    //float m_base_radius;
    //float m_height;
};

class Null_cylinder_shape
    : public Null_collision_shape
{
public:
    Null_cylinder_shape(const Axis axis, const glm::vec3 half_extents)
        //: m_axis{axis}
        //, m_half_extents{half_extents}
    {
        static_cast<void>(axis);
        static_cast<void>(half_extents);
    }

    ~Null_cylinder_shape() override = default;

private:
    //Axis m_axis;
    //glm::vec3 m_half_extents;
};


class Null_sphere_shape
    : public Null_collision_shape
{
public:
    explicit Null_sphere_shape(const float radius)
        //: m_radius{radius}
    {
        static_cast<void>(radius);
    }

    ~Null_sphere_shape() override = default;

private:
    //float m_radius;
};

} // namespace erhe::physics
