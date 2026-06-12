#pragma once

#include "erhe_physics/transform.hpp"

#include <glm/glm.hpp>

#include <array>
#include <limits>
#include <memory>
#include <optional>

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

// Per-axis limit for Six_dof_constraint_settings:
// free (default) / limited / fixed (min == max).
class Constraint_axis_limit
{
public:
    bool                 limited  {false};
    float                min      {0.0f};
    float                max      {0.0f};
    std::optional<float> stiffness{};      // soft limit spring; Jolt supports limit springs on translation axes only
    float                damping  {0.0f};
};

// Per-axis drive (motor) for Six_dof_constraint_settings.
class Constraint_axis_drive
{
public:
    bool  enabled            {false};
    bool  use_position_target{false}; // true when the spec drive stiffness > 0
    float position_target    {0.0f};  // meters (translation axes) or radians (rotation axes)
    float velocity_target    {0.0f};  // m/s (translation axes) or rad/s (rotation axes)
    float stiffness          {0.0f};
    float damping            {0.0f};
    float max_force          {std::numeric_limits<float>::infinity()}; // N (translation axes) or Nm (rotation axes)
};

class Six_dof_constraint_settings
{
public:
    IRigid_body* rigid_body_a{nullptr};
    IRigid_body* rigid_body_b{nullptr};   // nullptr = constrain to world; frame_in_b is then a world space frame
    Transform    frame_in_a  {};          // joint frame in body A local (node) space
    Transform    frame_in_b  {};          // joint frame in body B local (node) space
    std::array<Constraint_axis_limit, 6> limits{}; // 0..2 translation XYZ, 3..5 rotation XYZ
    std::array<Constraint_axis_drive, 6> drives{}; // 0..2 translation XYZ, 3..5 rotation XYZ
};

class IConstraint
{
public:
    virtual ~IConstraint() noexcept = default; // TODO move to .cpp;

    [[nodiscard]] static auto create_point_to_point_constraint(const Point_to_point_constraint_settings& settings) -> IConstraint*;

    [[nodiscard]] static auto create_point_to_point_constraint_shared(
        const Point_to_point_constraint_settings& settings
    ) -> std::shared_ptr<IConstraint>;

    [[nodiscard]] static auto create_point_to_point_constraint_unique(
        const Point_to_point_constraint_settings& settings
    ) -> std::unique_ptr<IConstraint>;

    [[nodiscard]] static auto create_six_dof_constraint(const Six_dof_constraint_settings& settings) -> IConstraint*;

    [[nodiscard]] static auto create_six_dof_constraint_shared(
        const Six_dof_constraint_settings& settings
    ) -> std::shared_ptr<IConstraint>;

    [[nodiscard]] static auto create_six_dof_constraint_unique(
        const Six_dof_constraint_settings& settings
    ) -> std::unique_ptr<IConstraint>;
};

} // namespace erhe::physics
