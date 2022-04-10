#pragma once

#include "erhe/physics/icollision_shape.hpp"
#include "erhe/physics/jolt/glm_conversions.hpp"
#include "erhe/toolkit/optional.hpp"
#include "erhe/toolkit/verify.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

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
    void calculate_local_inertia(float mass, glm::mat4& inertia) const override
    {
        JPH::MassProperties mass_properties = m_jolt_shape->GetMassProperties();
        mass_properties.ScaleToMass(mass);
        inertia = from_jolt(mass_properties.mInertia);
    }

    auto is_convex() const -> bool override
    {
        return true;
    }

    //virtual void calculate_principal_axis_transform(
    //    const std::vector<float>& /*child_masses*/,
    //    Transform&                /*principal_transform*/,
    //    glm::mat4&                /*inertia*/
    //) override
    //{
    //    //assert(false);
    //}

    auto get_jolt_shape() -> JPH::ShapeRefC
    {
        return m_jolt_shape;
    }

    virtual auto get_shape_settings() -> JPH::ShapeSettings& = 0;

protected:
    JPH::ShapeRefC m_jolt_shape;
};

class Jolt_box_shape
    : public Jolt_collision_shape
{
public:
    ~Jolt_box_shape() noexcept override = default;

    explicit Jolt_box_shape(const glm::vec3 half_extents)
        : m_shape_settings{to_jolt(half_extents)}
    {
	    auto result = m_shape_settings.Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    auto get_shape_settings() -> JPH::ShapeSettings& override
    {
        return m_shape_settings;
    }

private:
    JPH::BoxShapeSettings m_shape_settings;
};

class Jolt_capsule_shape
    : public Jolt_collision_shape
{
public:
    Jolt_capsule_shape(const Axis axis, const float radius, const float length)
        : m_shape_settings{length * 0.5f, radius}
    {
        ERHE_VERIFY(axis == Axis::Y);
	    auto result = m_shape_settings.Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    ~Jolt_capsule_shape() noexcept override = default;

    auto get_shape_settings() -> JPH::ShapeSettings& override
    {
        return m_shape_settings;
    }

private:
    JPH::CapsuleShapeSettings m_shape_settings;
};

#if 0
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
#endif

class Jolt_cylinder_shape
    : public Jolt_collision_shape
{
public:
    Jolt_cylinder_shape(const Axis axis, const glm::vec3 half_extents)
        : m_shape_settings{half_extents.x, half_extents.y}
    {
        ERHE_VERIFY(axis == Axis::Y);
	    auto result = m_shape_settings.Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    ~Jolt_cylinder_shape() noexcept override = default;

    auto get_shape_settings() -> JPH::ShapeSettings& override
    {
        return m_shape_settings;
    }

private:
    //Axis m_axis;
    //glm::vec3 m_half_extents;
    JPH::CylinderShapeSettings m_shape_settings;
};


class Jolt_sphere_shape
    : public Jolt_collision_shape
{
public:
    explicit Jolt_sphere_shape(const float radius)
        : m_shape_settings{radius}
    {
	    auto result = m_shape_settings.Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    ~Jolt_sphere_shape() noexcept override = default;

    auto get_shape_settings() -> JPH::ShapeSettings& override
    {
        return m_shape_settings;
    }

private:
    //float m_radius;
	JPH::SphereShapeSettings m_shape_settings;
};

} // namespace erhe::physics
