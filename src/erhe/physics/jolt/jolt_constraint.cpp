#include "erhe/physics/jolt/jolt_constraint.hpp"
#include "erhe/physics/jolt/jolt_rigid_body.hpp"

namespace erhe::physics
{

auto IConstraint::create_point_to_point_constraint(
    IRigid_body*    rigid_body,
    const glm::vec3 point
) -> IConstraint*
{
    return new Jolt_point_to_point_constraint(rigid_body, point);
}

auto IConstraint::create_point_to_point_constraint_shared(
    IRigid_body*    rigid_body,
    const glm::vec3 point
) -> std::shared_ptr<IConstraint>
{
    return std::make_shared<Jolt_point_to_point_constraint>(rigid_body, point);
}

auto IConstraint::create_point_to_point_constraint_unique(
    IRigid_body*    rigid_body,
    const glm::vec3 point
) -> std::unique_ptr<IConstraint>
{
    return std::make_unique<Jolt_point_to_point_constraint>(rigid_body, point);
}

auto IConstraint::create_point_to_point_constraint(
    IRigid_body*    rigid_body_a,
    IRigid_body*    rigid_body_b,
    const glm::vec3 pivot_in_a,
    const glm::vec3 pivot_in_b
) -> IConstraint*
{
    return new Jolt_point_to_point_constraint(rigid_body_a, rigid_body_b, pivot_in_a, pivot_in_b);
}

auto IConstraint::create_point_to_point_constraint_shared(
    IRigid_body*    rigid_body_a,
    IRigid_body*    rigid_body_b,
    const glm::vec3 pivot_in_a,
    const glm::vec3 pivot_in_b
) -> std::shared_ptr<IConstraint>
{
    return std::make_shared<Jolt_point_to_point_constraint>(rigid_body_a, rigid_body_b, pivot_in_a, pivot_in_b);
}
auto IConstraint::create_point_to_point_constraint_unique(
    IRigid_body*    rigid_body_a,
    IRigid_body*    rigid_body_b,
    const glm::vec3 pivot_in_a,
    const glm::vec3 pivot_in_b
) -> std::unique_ptr<IConstraint>
{
    return std::make_unique<Jolt_point_to_point_constraint>(rigid_body_a, rigid_body_b, pivot_in_a, pivot_in_b);
}

} // namespace erhe::physics
