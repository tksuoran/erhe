#include "erhe/physics/jolt/jolt_constraint.hpp"

namespace erhe::physics
{

auto IConstraint::create_point_to_point_constraint(
    const Point_to_point_constraint_settings& settings
) -> IConstraint*
{
    return new Jolt_point_to_point_constraint(settings);
}

auto IConstraint::create_point_to_point_constraint_shared(
    const Point_to_point_constraint_settings& settings
) -> std::shared_ptr<IConstraint>
{
    return std::make_shared<Jolt_point_to_point_constraint>(settings);
}
auto IConstraint::create_point_to_point_constraint_unique(
    const Point_to_point_constraint_settings& settings
) -> std::unique_ptr<IConstraint>
{
    return std::make_unique<Jolt_point_to_point_constraint>(settings);
}

} // namespace erhe::physics
