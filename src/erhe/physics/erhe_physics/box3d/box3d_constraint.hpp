#pragma once

#include "erhe_physics/iconstraint.hpp"

#include <box3d/box3d.h>

namespace erhe::physics {

class Box3d_constraint : public IConstraint
{
public:
    ~Box3d_constraint() noexcept override;

    [[nodiscard]] auto get_box3d_joint() const -> b3JointId { return m_joint; }
    [[nodiscard]] auto is_valid       () const -> bool      { return m_is_valid; }

    // Called by Box3d_world::update_fixed_step() right before b3World_Step()
    // for every constraint added to the world. A velocity drive is a
    // finite-gain viscous coupling (force = damping * (target - v)), while a
    // Box3D motor is a velocity constraint clamped to a maximum force, so the
    // six-DOF constraint re-derives that clamp from the current velocity
    // error here, each step.
    virtual void prepare_step(float dt);

protected:
    b3JointId m_joint   {};
    bool      m_is_valid{false};
};

} // namespace erhe::physics
