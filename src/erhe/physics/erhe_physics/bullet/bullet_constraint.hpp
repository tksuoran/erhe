#pragma once

#include "erhe_physics/iconstraint.hpp"

#include <BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h>
#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class IRigid_body;

class Bullet_constraint
    : public IConstraint
{
public:
    ~Bullet_constraint() noexcept override;

    [[nodiscard]] virtual auto get_bullet_constraint()       ->       btTypedConstraint* = 0;
    [[nodiscard]] virtual auto get_bullet_constraint() const -> const btTypedConstraint* = 0;
};

class Bullet_point_to_point_constraint
    : public Bullet_constraint
{
public:
    Bullet_point_to_point_constraint(
        const Point_to_point_constraint_settings& settings
    );

    // Implements Bullet_collision_shape
    [[nodiscard]] auto get_bullet_constraint() -> btTypedConstraint* override;
    [[nodiscard]] auto get_bullet_constraint() const -> const btTypedConstraint* override;

private:
    btPoint2PointConstraint m_bullet_constraint;
};

} // namespace erhe::physics
