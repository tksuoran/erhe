#include "erhe_physics/null/null_constraint.hpp"
#include "erhe_physics/joint_limits.hpp"
#include "erhe_physics/null/null_rigid_body.hpp"

namespace erhe::physics
{

// No simulation enforces anything; the authored limits are reported as the
// shape, in the pyramid convention of the default backend.
auto get_enforced_joint_limits(const std::array<Constraint_axis_limit, 6>& limits) -> Joint_limit_shape
{
    Joint_limit_shape shape{};
    shape.translation = {limits[0], limits[1], limits[2]};
    shape.twist_axis  = 0;
    shape.twist       = limits[3];
    shape.swing_model = Swing_limit_model::pyramid;
    shape.swing       = {limits[4], limits[5]};
    shape.is_exact    = true;
    return shape;
}

auto IConstraint::create_point_to_point_constraint(
    const Point_to_point_constraint_settings& settings
) -> IConstraint*
{
    return new Null_point_to_point_constraint(settings);
}

auto IConstraint::create_point_to_point_constraint_shared(
    const Point_to_point_constraint_settings& settings
) -> std::shared_ptr<IConstraint>
{
    return std::make_shared<Null_point_to_point_constraint>(settings);
}

auto IConstraint::create_point_to_point_constraint_unique(
    const Point_to_point_constraint_settings& settings
) -> std::unique_ptr<IConstraint>
{
    return std::make_unique<Null_point_to_point_constraint>(settings);
}

auto IConstraint::create_six_dof_constraint(
    const Six_dof_constraint_settings& settings
) -> IConstraint*
{
    return new Null_six_dof_constraint(settings);
}

auto IConstraint::create_six_dof_constraint_shared(
    const Six_dof_constraint_settings& settings
) -> std::shared_ptr<IConstraint>
{
    return std::make_shared<Null_six_dof_constraint>(settings);
}

auto IConstraint::create_six_dof_constraint_unique(
    const Six_dof_constraint_settings& settings
) -> std::unique_ptr<IConstraint>
{
    return std::make_unique<Null_six_dof_constraint>(settings);
}

} // namespace erhe::physics
