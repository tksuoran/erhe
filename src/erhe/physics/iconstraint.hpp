#pragma once

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class IRigid_body;

class IConstraint
{
public:
    virtual ~IConstraint() {};

    [[nodiscard]] static auto create_point_to_point_constraint       (IRigid_body* rigid_body, const glm::vec3 point) -> IConstraint*;
    [[nodiscard]] static auto create_point_to_point_constraint_shared(IRigid_body* rigid_body, const glm::vec3 point) -> std::shared_ptr<IConstraint>;
    [[nodiscard]] static auto create_point_to_point_constraint_unique(IRigid_body* rigid_body, const glm::vec3 point) -> std::unique_ptr<IConstraint>;

    [[nodiscard]] static auto create_point_to_point_constraint(
        IRigid_body*     rigid_body_a,
        IRigid_body*     rigid_body_b,
        const glm::vec3  pivot_in_a,
        const glm::vec3  pivot_in_b
    ) -> IConstraint*;

    [[nodiscard]] static auto create_point_to_point_constraint_shared(
        IRigid_body*    rigid_body_a,
        IRigid_body*    rigid_body_b,
        const glm::vec3 pivot_in_a,
        const glm::vec3 pivot_in_b
    ) -> std::shared_ptr<IConstraint>;

    [[nodiscard]] static auto create_point_to_point_constraint_unique(
        IRigid_body*    rigid_body_a,
        IRigid_body*    rigid_body_b,
        const glm::vec3 pivot_in_a,
        const glm::vec3 pivot_in_b
    ) -> std::unique_ptr<IConstraint>;

    [[nodiscard]] virtual auto get_pivot_in_a() -> glm::vec3 = 0;
    [[nodiscard]] virtual auto get_pivot_in_b() -> glm::vec3 = 0;
    virtual void set_pivot_in_a   (const glm::vec3 pivot_in_a) = 0;
    virtual void set_pivot_in_b   (const glm::vec3 pivot_in_b) = 0;
    virtual void set_impulse_clamp(const float impulse_clamp)  = 0;
    virtual void set_damping      (const float damping)        = 0;
    virtual void set_tau          (const float tau)            = 0;
};

} // namespace erhe::physics
