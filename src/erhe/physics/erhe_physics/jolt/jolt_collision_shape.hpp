#pragma once

#include "erhe_physics/icollision_shape.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <glm/glm.hpp>

namespace erhe::physics {

class Jolt_collision_shape : public ICollision_shape
{
public:
    ~Jolt_collision_shape() noexcept override = default;

    // Implements ICollision_shape
    void calculate_local_inertia(float mass, glm::mat4& inertia) const override;
    auto is_convex              () const -> bool                       override;
    auto get_center_of_mass     () const -> glm::vec3                  override;
    auto get_mass_properties    () const -> Mass_properties            override;
    auto describe               () const -> std::string                override;

    [[nodiscard]] auto get_jolt_shape() -> JPH::ShapeRefC;

    virtual auto get_shape_settings() -> JPH::ShapeSettings& = 0;

protected:
    JPH::ShapeRefC m_jolt_shape;
};

class Jolt_box_shape : public Jolt_collision_shape
{
public:
    ~Jolt_box_shape() noexcept override = default;

    explicit Jolt_box_shape(glm::vec3 half_extents);
    auto get_shape_settings() -> JPH::ShapeSettings& override;
    [[nodiscard]] auto describe() const -> std::string override;

private:
    JPH::Ref<JPH::BoxShapeSettings> m_shape_settings;
};

class Jolt_capsule_shape : public Jolt_collision_shape
{
public:
    Jolt_capsule_shape(Axis axis, float radius, float length);
    ~Jolt_capsule_shape() noexcept override;

    auto get_shape_settings() -> JPH::ShapeSettings& override;
    [[nodiscard]] auto describe() const -> std::string   override;

private:
    JPH::Ref<JPH::CapsuleShapeSettings>           m_capsule_shape_settings;
    JPH::Ref<JPH::RotatedTranslatedShapeSettings> m_shape_settings;
};

class Jolt_cylinder_shape : public Jolt_collision_shape
{
public:
    Jolt_cylinder_shape(Axis axis, glm::vec3 half_extents);
    ~Jolt_cylinder_shape() noexcept override;
    auto get_shape_settings() -> JPH::ShapeSettings& override;
    [[nodiscard]] auto describe() const -> std::string   override;

private:
    JPH::Ref<JPH::CylinderShapeSettings>          m_cylinder_shape_settings;
    JPH::Ref<JPH::RotatedTranslatedShapeSettings> m_shape_settings;
};


class Jolt_sphere_shape : public Jolt_collision_shape
{
public:
    explicit Jolt_sphere_shape(float radius);
    ~Jolt_sphere_shape() noexcept override;

    auto get_shape_settings() -> JPH::ShapeSettings& override;
    [[nodiscard]] auto describe() const -> std::string   override;

private:
    JPH::Ref<JPH::SphereShapeSettings> m_shape_settings;
};

} // namespace erhe::physics
