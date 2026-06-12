#include "erhe_physics/null/null_constraint.hpp"
#include "erhe_physics/null/null_rigid_body.hpp"

namespace erhe::physics
{

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
