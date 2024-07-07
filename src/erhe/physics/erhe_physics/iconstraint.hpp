#pragma once

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics {

class IRigid_body;

class Point_to_point_constraint_settings
{
public:
    IRigid_body* rigid_body_a{nullptr};
    IRigid_body* rigid_body_b{nullptr};
    glm::vec3    pivot_in_a  {0.0f, 0.0f, 0.0f};
    glm::vec3    pivot_in_b  {0.0f, 0.0f, 0.0f};
    float        frequency   {1.0f};
    float        damping     {1.0f};
};

class IConstraint
{
public:
    virtual ~IConstraint() noexcept {};

    [[nodiscard]] static auto create_point_to_point_constraint(const Point_to_point_constraint_settings& settings) -> IConstraint*;

    [[nodiscard]] static auto create_point_to_point_constraint_shared(
        const Point_to_point_constraint_settings& settings
    ) -> std::shared_ptr<IConstraint>;

    [[nodiscard]] static auto create_point_to_point_constraint_unique(
        const Point_to_point_constraint_settings& settings
    ) -> std::unique_ptr<IConstraint>;
};

} // namespace erhe::physics
