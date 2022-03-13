#pragma once

#include "erhe/physics/iconstraint.hpp"

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
        IRigid_body*    rigid_body,
        const glm::vec3 pivot_in_a
    );

    Bullet_point_to_point_constraint(
        IRigid_body*    rigid_body_a,
        IRigid_body*    rigid_body_b,
        const glm::vec3 pivot_in_a,
        const glm::vec3 pivot_in_b
    );

    // Implements IConstraint
    [[nodiscard]] auto get_pivot_in_a() -> glm::vec3   override;
    [[nodiscard]] auto get_pivot_in_b() -> glm::vec3   override;
    void set_pivot_in_a   (const glm::vec3 pivot_in_a) override;
    void set_pivot_in_b   (const glm::vec3 pivot_in_b) override;
    void set_impulse_clamp(const float impulse_clamp)  override;
    void set_damping      (const float damping)        override;
    void set_tau          (const float tau)            override;

    // Implements Bullet_collision_shape
    [[nodiscard]] auto get_bullet_constraint() -> btTypedConstraint* override;
    [[nodiscard]] auto get_bullet_constraint() const -> const btTypedConstraint* override;

private:
    btPoint2PointConstraint m_bullet_constraint;
};

} // namespace erhe::physics
