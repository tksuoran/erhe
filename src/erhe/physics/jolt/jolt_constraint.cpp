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

Jolt_point_to_point_constraint::Jolt_point_to_point_constraint(
    const Point_to_point_constraint_settings& settings
)
{
    m_settings.mSpace       = JPH::EConstraintSpace::LocalToBodyCOM;
    m_settings.mPoint1      = to_jolt(settings.pivot_in_a);
    m_settings.mPoint2      = to_jolt(settings.pivot_in_b);
    m_settings.mMinDistance = 0.0f;
    m_settings.mMaxDistance = 0.0f;
    auto* const body_a = reinterpret_cast<Jolt_rigid_body*>(settings.rigid_body_a)->get_jolt_body();
    auto* const body_b = reinterpret_cast<Jolt_rigid_body*>(settings.rigid_body_b)->get_jolt_body();
    m_constraint = m_settings.Create(
        *body_a,
        *body_b
    );
}

Jolt_point_to_point_constraint::~Jolt_point_to_point_constraint() noexcept
{
    // TODO destroy
}

auto Jolt_point_to_point_constraint::get_jolt_constraint() const -> JPH::Constraint*
{
    return m_constraint;
}

} // namespace erhe::physics
