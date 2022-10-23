#include "erhe/physics/bullet/bullet_constraint.hpp"
#include "erhe/physics/bullet/bullet_rigid_body.hpp"
#include "erhe/physics/bullet/glm_conversions.hpp"
#include "erhe/physics/physics_log.hpp"

namespace erhe::physics
{

auto IConstraint::create_point_to_point_constraint(
    const Point_to_point_constraint_settings& settings
) -> IConstraint*
{
    return new Bullet_point_to_point_constraint(settings);
}

auto IConstraint::create_point_to_point_constraint_shared(
    const Point_to_point_constraint_settings& settings
) -> std::shared_ptr<IConstraint>
{
    return std::make_shared<Bullet_point_to_point_constraint>(settings);
}

auto IConstraint::create_point_to_point_constraint_unique(
    const Point_to_point_constraint_settings& settings
) -> std::unique_ptr<IConstraint>
{
    return std::make_unique<Bullet_point_to_point_constraint>(settings);
}

Bullet_constraint::~Bullet_constraint() noexcept
{
}

Bullet_point_to_point_constraint::Bullet_point_to_point_constraint(
    const Point_to_point_constraint_settings& settings
)
    : m_bullet_constraint{
        *static_cast<Bullet_rigid_body*>(settings.rigid_body_a)->get_bullet_rigid_body(),
        *static_cast<Bullet_rigid_body*>(settings.rigid_body_b)->get_bullet_rigid_body(),
        to_bullet(settings.pivot_in_a),
        to_bullet(settings.pivot_in_b)
    }
{
    m_bullet_constraint.m_setting.m_tau = 0.001f;
    m_bullet_constraint.m_setting.m_damping = settings.damping;
    m_bullet_constraint.m_setting.m_impulseClamp = 1.0f;
}

auto Bullet_point_to_point_constraint::get_bullet_constraint() -> btTypedConstraint*
{
    return &m_bullet_constraint;
}

auto Bullet_point_to_point_constraint::get_bullet_constraint() const -> const btTypedConstraint*
{
    return &m_bullet_constraint;
}

} // namespace erhe::physics
