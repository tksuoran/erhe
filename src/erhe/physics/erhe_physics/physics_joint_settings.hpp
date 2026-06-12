#pragma once

#include "erhe_item/item.hpp"

#include <array>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace erhe::physics {

// One per-axis limit entry of a KHR_physics_rigid_bodies joint description.
// Axis flags select which linear / angular axes (X, Y, Z) the limit applies
// to. min == max means the axis is fixed; absent min/max leave that side
// unbounded. stiffness (when present) makes the limit soft (spring).
class Joint_limit
{
public:
    std::array<bool, 3>  linear_axes {false, false, false};
    std::array<bool, 3>  angular_axes{false, false, false};
    std::optional<float> min;
    std::optional<float> max;
    std::optional<float> stiffness; // soft limit spring; nullopt = hard limit
    float                damping{0.0f};
};

enum class Drive_type : int {
    e_linear  = 0,
    e_angular = 1
};

enum class Drive_mode : int {
    e_force        = 0,
    e_acceleration = 1
};

// One drive (motor) entry of a KHR_physics_rigid_bodies joint description.
// stiffness > 0 selects a position motor (position_target + spring), else a
// velocity motor (velocity_target).
class Joint_drive
{
public:
    Drive_type type           {Drive_type::e_linear};
    Drive_mode mode           {Drive_mode::e_force};
    int        axis           {0};
    float      max_force      {std::numeric_limits<float>::infinity()};
    float      position_target{0.0f};
    float      velocity_target{0.0f};
    float      stiffness      {0.0f};
    float      damping        {0.0f};
};

// Shared joint settings asset (KHR_physics_rigid_bodies physicsJoints entry).
// Data only: constraints are built from this in the Six-DOF constraint
// wrapper (see iconstraint.hpp) by the editor's Node_joint attachment.
class Physics_joint_settings : public erhe::Item<erhe::Item_base, erhe::Item_base, Physics_joint_settings>
{
public:
    Physics_joint_settings();
    explicit Physics_joint_settings(std::string_view name);
    explicit Physics_joint_settings(const Physics_joint_settings&);
    Physics_joint_settings& operator=(const Physics_joint_settings&);
    ~Physics_joint_settings() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Physics_joint_settings"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::physics_joint_settings; }

    std::vector<Joint_limit> limits;
    std::vector<Joint_drive> drives;
};

} // namespace erhe::physics
