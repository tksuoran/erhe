#pragma once

#include "erhe/physics/iconstraint.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/physics/jolt/jolt_rigid_body.hpp"
#include "erhe/physics/jolt/glm_conversions.hpp"
#include "erhe/physics/physics_log.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>

#include <glm/glm.hpp>

#include <memory>

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
    explicit Jolt_point_to_point_constraint(
        const Point_to_point_constraint_settings& settings
    )
    {
        m_settings.mSpace       = JPH::EConstraintSpace::LocalToBodyCOM;
        m_settings.mPoint1      = to_jolt(settings.pivot_in_a);
        m_settings.mPoint2      = to_jolt(settings.pivot_in_b);
        m_settings.mMinDistance = 0.0f;
        m_settings.mMaxDistance = 0.0f;
        m_settings.mFrequency   = settings.frequency;
        m_settings.mDamping     = settings.damping;
        auto* const body_a = reinterpret_cast<Jolt_rigid_body*>(settings.rigid_body_a)->get_jolt_body();
        auto* const body_b = reinterpret_cast<Jolt_rigid_body*>(settings.rigid_body_b)->get_jolt_body();
        m_constraint = m_settings.Create(
            *body_a,
            *body_b
        );
    }

    [[nodiscard]] auto get_jolt_constraint() const -> JPH::Constraint* override
    {
        return m_constraint;
    }

private:
    //JPH::PointConstraintSettings m_settings;
    JPH::DistanceConstraintSettings m_settings;
    JPH::Constraint*                m_constraint;
};

} // namespace erhe::physics
