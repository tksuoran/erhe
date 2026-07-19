#include "erhe_physics/box3d/box3d_constraint.hpp"
#include "erhe_physics/physics_log.hpp"

namespace erhe::physics {

Box3d_constraint::~Box3d_constraint() noexcept
{
    if (m_is_valid) {
        b3DestroyJoint(m_joint, true);
        m_is_valid = false;
    }
}

// Constraints are not implemented yet: point-to-point maps onto a Box3D
// spherical joint and the six-DOF settings are classified into the closest
// Box3D joint (see box3d_six_dof_classifier), but neither is wired up. Until
// then these report the gap instead of returning an object that looks like a
// working constraint.

auto IConstraint::create_point_to_point_constraint(const Point_to_point_constraint_settings&) -> IConstraint*
{
    log_physics->error("box3d: point-to-point constraints are not implemented yet");
    return nullptr;
}

auto IConstraint::create_point_to_point_constraint_shared(
    const Point_to_point_constraint_settings&
) -> std::shared_ptr<IConstraint>
{
    log_physics->error("box3d: point-to-point constraints are not implemented yet");
    return {};
}

auto IConstraint::create_point_to_point_constraint_unique(
    const Point_to_point_constraint_settings&
) -> std::unique_ptr<IConstraint>
{
    log_physics->error("box3d: point-to-point constraints are not implemented yet");
    return {};
}

auto IConstraint::create_six_dof_constraint(const Six_dof_constraint_settings&) -> IConstraint*
{
    log_physics->error("box3d: six-dof constraints are not implemented yet");
    return nullptr;
}

auto IConstraint::create_six_dof_constraint_shared(
    const Six_dof_constraint_settings&
) -> std::shared_ptr<IConstraint>
{
    log_physics->error("box3d: six-dof constraints are not implemented yet");
    return {};
}

auto IConstraint::create_six_dof_constraint_unique(
    const Six_dof_constraint_settings&
) -> std::unique_ptr<IConstraint>
{
    log_physics->error("box3d: six-dof constraints are not implemented yet");
    return {};
}

} // namespace erhe::physics
