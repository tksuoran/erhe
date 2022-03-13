#pragma once

#include "erhe/physics/icollision_shape.hpp"
#include "erhe/physics/jolt/glm_conversions.hpp"
#include "erhe/toolkit/optional.hpp"
#include "erhe/toolkit/verify.hpp"

#include <Jolt.h>
#include <Physics/Collision/Shape/BoxShape.h>
#include <Physics/Collision/Shape/CapsuleShape.h>
#include <Physics/Collision/Shape/CylinderShape.h>
#include <Physics/Collision/Shape/SphereShape.h>

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class Jolt_collision_shape
    : public ICollision_shape
{
public:
    ~Jolt_collision_shape() noexcept override = default;

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

    virtual void calculate_principal_axis_transform(
        const std::vector<float>& /*child_masses*/,
        Transform&                /*principal_transform*/,
        glm::vec3&                /*inertia*/
    ) override
    {
        //assert(false);
    }

    auto get_jolt_shape() -> JPH::ShapeRefC
    {
        return m_jolt_shape;
    }

protected:
    JPH::ShapeRefC m_jolt_shape;
};

class Jolt_box_shape
    : public Jolt_collision_shape
{
public:
    ~Jolt_box_shape() noexcept override = default;

    explicit Jolt_box_shape(const glm::vec3 half_extents)
    {
	    JPH::BoxShapeSettings shape_settings{to_jolt(half_extents)};
	    auto result = shape_settings.Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }
};

class Jolt_capsule_shape
    : public Jolt_collision_shape
{
public:
    Jolt_capsule_shape(const Axis axis, const float radius, const float length)
    {
        ERHE_VERIFY(axis == Axis::Y);
	    JPH::CapsuleShapeSettings shape_settings{length * 0.5f, radius};
	    auto result = shape_settings.Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    ~Jolt_capsule_shape() noexcept override = default;
};


class Jolt_cone_shape
    : public Jolt_collision_shape
{
public:
    Jolt_cone_shape(const Axis axis, const float base_radius, const float height)
    {
        static_cast<void>(axis);
        static_cast<void>(base_radius);
        static_cast<void>(height);
    }

    ~Jolt_cone_shape() noexcept override = default;

private:
    //Axis  m_axis;
    //float m_base_radius;
    //float m_height;
};

class Jolt_cylinder_shape
    : public Jolt_collision_shape
{
public:
    Jolt_cylinder_shape(const Axis axis, const glm::vec3 half_extents)
    {
        ERHE_VERIFY(axis == Axis::Y);
	    JPH::CylinderShapeSettings shape_settings{half_extents.x, half_extents.y};
	    auto result = shape_settings.Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    ~Jolt_cylinder_shape() noexcept override = default;

private:
    //Axis m_axis;
    //glm::vec3 m_half_extents;
};


class Jolt_sphere_shape
    : public Jolt_collision_shape
{
public:
    explicit Jolt_sphere_shape(const float radius)
    {
	    JPH::SphereShapeSettings shape_settings{radius};
	    auto result = shape_settings.Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    ~Jolt_sphere_shape() noexcept override = default;

private:
    //float m_radius;
};

} // namespace erhe::physics
