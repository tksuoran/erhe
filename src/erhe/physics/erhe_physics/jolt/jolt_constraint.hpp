#pragma once

#include "erhe_physics/iconstraint.hpp"
#include "erhe_physics/jolt/jolt_rigid_body.hpp"
#include "erhe_physics/jolt/glm_conversions.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>

#include <array>

namespace erhe::physics {

class IRigid_body;

class Jolt_constraint : public IConstraint
{
public:
    [[nodiscard]] virtual auto get_jolt_constraint() const -> JPH::Constraint* = 0;

    // Called by Jolt_world::update_fixed_step() right before the physics
    // system update for every constraint added to the world. A velocity
    // drive is a finite-gain viscous coupling (force = damping * (target - v)),
    // while a Jolt velocity motor is a velocity constraint bounded by a force,
    // so the six-DOF constraint re-derives that bound from the current
    // velocity error here, each step.
    virtual void prepare_step(float dt);
};

class Jolt_point_to_point_constraint : public Jolt_constraint
{
public:
    explicit Jolt_point_to_point_constraint(const Point_to_point_constraint_settings& settings);
    ~Jolt_point_to_point_constraint() noexcept override;

    [[nodiscard]] auto get_jolt_constraint() const -> JPH::Constraint* override;

private:
    JPH::Ref<JPH::Constraint> m_constraint;
};

class Jolt_six_dof_constraint : public Jolt_constraint
{
public:
    explicit Jolt_six_dof_constraint(const Six_dof_constraint_settings& settings);
    ~Jolt_six_dof_constraint() noexcept override;

    [[nodiscard]] auto get_jolt_constraint() const -> JPH::Constraint* override;
    void prepare_step(float dt) override;

private:
    class Velocity_drive
    {
    public:
        bool  active   {false};
        float gain     {0.0f}; // force per unit velocity error; infinity = hard motor
        float max_force{0.0f}; // the authored bound; infinity = unbounded
        float target   {0.0f};
    };

    JPH::Ref<JPH::SixDOFConstraint> m_constraint;
    JPH::Body*                      m_body_a{nullptr};
    JPH::Body*                      m_body_b{nullptr};
    std::array<Velocity_drive, 6>   m_velocity_drives{};
};

} // namespace erhe::physics
