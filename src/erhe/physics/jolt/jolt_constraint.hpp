#pragma once

#include "erhe/physics/iconstraint.hpp"
#include "erhe/physics/jolt/jolt_rigid_body.hpp"
#include "erhe/physics/jolt/glm_conversions.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>

namespace erhe::physics
{

class IRigid_body;

class Jolt_constraint
    : public IConstraint
{
public:
    [[nodiscard]] virtual auto get_jolt_constraint() const -> JPH::Constraint* = 0;
};

class Jolt_point_to_point_constraint
    : public Jolt_constraint
{
public:
    explicit Jolt_point_to_point_constraint(const Point_to_point_constraint_settings& settings);
    ~Jolt_point_to_point_constraint() noexcept;

    [[nodiscard]] auto get_jolt_constraint() const -> JPH::Constraint* override;

private:
    JPH::DistanceConstraintSettings m_settings;
    JPH::Constraint*                m_constraint;
};

} // namespace erhe::physics
