#pragma once

#include "erhe/physics/iconstraint.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/physics/jolt/jolt_rigid_body.hpp"
#include "erhe/physics/jolt/glm_conversions.hpp"
#include "erhe/physics/physics_log.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>

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
    //// Jolt_point_to_point_constraint(
    ////     IRigid_body*    rigid_body_a,
    ////     const glm::vec3 pivot_in_a
    //// )
    ////     //: m_rigid_body_a{rigid_body_a}
    ////     //, m_rigid_body_b{nullptr}
    ////     : m_pivot_in_a{pivot_in_a}
    ////     , m_pivot_in_b{0.0f}
    //// {
    ////     static_cast<void>(rigid_body_a);
    //// }

    Jolt_point_to_point_constraint(
        IRigid_body*    rigid_body_a,
        IRigid_body*    rigid_body_b,
        const glm::vec3 pivot_in_a,
        const glm::vec3 pivot_in_b
    )
        : m_rigid_body_a{rigid_body_a}
        , m_rigid_body_b{rigid_body_b}
    {
        auto* body_a = reinterpret_cast<Jolt_rigid_body*>(rigid_body_a)->get_jolt_body();
        auto* body_b = reinterpret_cast<Jolt_rigid_body*>(rigid_body_b)->get_jolt_body();
        const JPH::Mat44 transform_a = body_a->GetCenterOfMassTransform();
        const JPH::Mat44 transform_b = body_a->GetCenterOfMassTransform();

        m_settings.mPoint1 = transform_a * to_jolt(pivot_in_a);
        m_settings.mPoint2 = transform_b * to_jolt(pivot_in_b);
        m_constraint = m_settings.Create(
            *body_a,
            *body_b
        );
    }

    // Implements IConstraint
    void set_pivot_in_a(const glm::vec3 pivot_in_a) override
    {
        auto* body_a = reinterpret_cast<Jolt_rigid_body*>(m_rigid_body_a)->get_jolt_body();
        const JPH::Mat44 transform_a = body_a->GetCenterOfMassTransform();
        log_physics_frame->trace("A: {}, B: {}", pivot_in_a, from_jolt(m_settings.mPoint2));
        m_settings.mPoint1 = transform_a * to_jolt(pivot_in_a);
        reinterpret_cast<JPH::PointConstraint*>(m_constraint)->SetPoint1(m_settings.mPoint1);
    }

    void set_pivot_in_b(const glm::vec3 pivot_in_b) override
    {
        auto* body_b = reinterpret_cast<Jolt_rigid_body*>(m_rigid_body_b)->get_jolt_body();
        const JPH::Mat44 transform_b = body_b->GetCenterOfMassTransform();
        log_physics_frame->trace("B: {}, A: {}", pivot_in_b, from_jolt(m_settings.mPoint1));
        m_settings.mPoint2 = to_jolt(pivot_in_b);
        reinterpret_cast<JPH::PointConstraint*>(m_constraint)->SetPoint2(m_settings.mPoint2);
    }

    auto get_pivot_in_a() -> glm::vec3 override
    {
        return from_jolt(m_settings.mPoint1);
    }

    auto get_pivot_in_b() -> glm::vec3 override
    {
        return from_jolt(m_settings.mPoint2);
    }

    void set_impulse_clamp(const float impulse_clamp) override
    {
        static_cast<void>(impulse_clamp);
    }

    void set_damping(const float damping) override
    {
        static_cast<void>(damping);
    }

    void set_tau(const float tau) override
    {
        static_cast<void>(tau);
    }

    [[nodiscard]] auto get_jolt_constraint() const -> JPH::Constraint* override
    {
        return m_constraint;
    }

private:
    JPH::PointConstraintSettings m_settings;
    JPH::Constraint*             m_constraint;
    IRigid_body*                 m_rigid_body_a{nullptr};
    IRigid_body*                 m_rigid_body_b{nullptr};
    //glm::vec3   m_pivot_in_a;
    //glm::vec3   m_pivot_in_b;
};

} // namespace erhe::physics
