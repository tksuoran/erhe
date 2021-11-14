#pragma once

#include "erhe/physics/iconstraint.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class IRigid_body;

class Null_point_to_point_constraint
    : public IConstraint
{
public:
    Null_point_to_point_constraint(
        IRigid_body*    rigid_body_a,
        const glm::vec3 pivot_in_a
    )
        //: m_rigid_body_a{rigid_body_a}
        //, m_rigid_body_b{nullptr}
        : m_pivot_in_a{pivot_in_a}
        , m_pivot_in_b{0.0f}
    {
        static_cast<void>(rigid_body_a);
    }

    Null_point_to_point_constraint(
        IRigid_body*    rigid_body_a,
        IRigid_body*    rigid_body_b,
        const glm::vec3 pivot_in_a,
        const glm::vec3 pivot_in_b
    )
        //: m_rigid_body_a{rigid_body_a}
        //, m_rigid_body_b{rigid_body_b}
        : m_pivot_in_a{pivot_in_a}
        , m_pivot_in_b{pivot_in_b}
    {
        static_cast<void>(rigid_body_a);
        static_cast<void>(rigid_body_b);
    }

    // Implements IConstraint
    void set_pivot_in_a   (const glm::vec3 pivot_in_a) override
    {
        m_pivot_in_a = pivot_in_a;
    }

    void set_pivot_in_b   (const glm::vec3 pivot_in_b) override
    {
        m_pivot_in_b = pivot_in_b;
    }

    auto get_pivot_in_a   () -> glm::vec3              override
    {
        return m_pivot_in_a;
    }

    auto get_pivot_in_b   () -> glm::vec3              override
    {
        return m_pivot_in_b;
    }

    void set_impulse_clamp(const float impulse_clamp)  override
    {
        static_cast<void>(impulse_clamp);
    }

    void set_damping      (const float damping)        override
    {
        static_cast<void>(damping);
    }

    void set_tau          (const float tau)            override
    {
        static_cast<void>(tau);
    }

private:
    //IRigid_body* m_rigid_body_a{nullptr};
    //IRigid_body* m_rigid_body_b{nullptr};
    glm::vec3   m_pivot_in_a;
    glm::vec3   m_pivot_in_b;
};

} // namespace erhe::physics
