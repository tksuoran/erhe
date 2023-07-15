#pragma once

#include "erhe/physics/icollision_shape.hpp"
#include "erhe/physics/jolt/glm_conversions.hpp"
#include "erhe/toolkit/verify.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <glm/glm.hpp>

namespace erhe::physics
{

class Jolt_collision_shape
    : public ICollision_shape
{
public:
    ~Jolt_collision_shape() noexcept override = default;

    // Implements ICollision_shape
    void calculate_local_inertia(float mass, glm::mat4& inertia) const override;
    [[nodiscard]] auto is_convex          () const -> bool override;
    [[nodiscard]] auto get_center_of_mass () const -> glm::vec3 override;
    [[nodiscard]] auto get_mass_properties() const -> Mass_properties override;

    [[nodiscard]] auto get_jolt_shape() -> JPH::ShapeRefC;

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
    {
        m_shape_settings = new JPH::BoxShapeSettings(to_jolt(half_extents));
        auto result = m_shape_settings->Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    auto get_shape_settings() -> JPH::ShapeSettings& override
    {
        return *m_shape_settings.GetPtr();
    }

private:
    JPH::Ref<JPH::BoxShapeSettings> m_shape_settings;
};

class Jolt_capsule_shape
    : public Jolt_collision_shape
{
public:
    Jolt_capsule_shape(const Axis axis, const float radius, const float length)
    {
        m_capsule_shape_settings = new JPH::CapsuleShapeSettings(length * 0.5f, radius);
        const JPH::Vec3 x_axis{1.0f, 0.0f, 0.0f};
        const JPH::Vec3 y_axis{0.0f, 1.0f, 0.0f};
        const JPH::Vec3 z_axis{0.0f, 0.0f, 1.0f};
        m_shape_settings = new JPH::RotatedTranslatedShapeSettings{
            JPH::Vec3{0.0f, 0.0f, 0.0f},
            (axis == Axis::Y)
                ? JPH::Quat::sIdentity()
                : (axis == Axis::X)
                    ? JPH::Quat::sFromTo(y_axis, x_axis)
                    : JPH::Quat::sFromTo(y_axis, z_axis),
            m_capsule_shape_settings
        };
        auto result = m_shape_settings->Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    ~Jolt_capsule_shape() noexcept override
    {
    }

    auto get_shape_settings() -> JPH::ShapeSettings& override
    {
        return *m_shape_settings.GetPtr();
    }

private:
    JPH::Ref<JPH::CapsuleShapeSettings>           m_capsule_shape_settings;
    JPH::Ref<JPH::RotatedTranslatedShapeSettings> m_shape_settings;
};

#if 0
class Jolt_cone_shape
    : public Jolt_collision_shape
{
public:
    Jolt_cone_shape(const Axis axis, const float base_radius, const float height)
        : m_shape_settings{height * 0.5f, 0.0f, base_radius}
    {
        ERHE_VERIFY(axis == Axis::Y);
        auto result = m_shape_settings.Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    ~Jolt_cone_shape() noexcept override = default;

    auto get_shape_settings() -> JPH::ShapeSettings& override
    {
        return m_shape_settings;
    }

private:
    JPH::TaperedCapsuleShapeSettings m_shape_settings;
};
#endif

class Jolt_cylinder_shape
    : public Jolt_collision_shape
{
public:
    Jolt_cylinder_shape(const Axis axis, const glm::vec3 half_extents)
    {
        m_cylinder_shape_settings = new JPH::CylinderShapeSettings(half_extents.x, half_extents.y);
        const JPH::Vec3 x_axis{1.0f, 0.0f, 0.0f};
        const JPH::Vec3 y_axis{0.0f, 1.0f, 0.0f};
        const JPH::Vec3 z_axis{0.0f, 0.0f, 1.0f};
        m_shape_settings = new JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3{0.0f, 0.0f, 0.0f},
            (axis == Axis::Y)
                ? JPH::Quat::sIdentity()
                : (axis == Axis::X)
                    ? JPH::Quat::sFromTo(y_axis, x_axis)
                    : JPH::Quat::sFromTo(y_axis, z_axis),
            m_cylinder_shape_settings
        );
        auto result = m_shape_settings->Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    ~Jolt_cylinder_shape() noexcept override = default;

    auto get_shape_settings() -> JPH::ShapeSettings& override
    {
        return *m_shape_settings.GetPtr();
    }

private:
    JPH::Ref<JPH::CylinderShapeSettings>          m_cylinder_shape_settings;
    JPH::Ref<JPH::RotatedTranslatedShapeSettings> m_shape_settings;
};


class Jolt_sphere_shape
    : public Jolt_collision_shape
{
public:
    explicit Jolt_sphere_shape(const float radius)
    {
        m_shape_settings = new JPH::SphereShapeSettings(radius);
        auto result = m_shape_settings->Create();
        ERHE_VERIFY(result.IsValid());
        m_jolt_shape = result.Get();
    }

    ~Jolt_sphere_shape() noexcept override = default;

    auto get_shape_settings() -> JPH::ShapeSettings& override
    {
        return *m_shape_settings.GetPtr();
    }

private:
    JPH::Ref<JPH::SphereShapeSettings> m_shape_settings;
};

} // namespace erhe::physics
