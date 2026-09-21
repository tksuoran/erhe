#pragma once

#include "erhe_item/item.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_physics/iconstraint.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/enum_info.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace erhe::physics {

// What a constraint does with one degree of freedom.
//   free    : the constraint leaves the axis alone
//   limited : the axis is confined to [min, max]; a fixed axis is min == max
enum class Joint_axis_limit : int {
    free    = 0,
    limited = 1
};

// The motor of one degree of freedom. `off` is the axis with no motor, so a
// drive that is described but disabled is unrepresentable.
enum class Joint_axis_drive : int {
    off          = 0,
    force        = 1,
    acceleration = 2
};

extern const erhe::property::Enum_info c_joint_axis_limit_enum_info;
extern const erhe::property::Enum_info c_joint_axis_drive_enum_info;

// The six degrees of freedom, in the order of
// Six_dof_constraint_settings::limits and ::drives: indices 0..2 are the
// translation axes X, Y, Z and indices 3..5 the rotation axes X, Y, Z. The
// token of an axis is the prefix of every property name of that axis, and it
// is the axis token of the USD multi-apply instances the writer authors.
inline constexpr std::size_t c_joint_axis_count = 6;

[[nodiscard]] auto joint_axis_token   (std::size_t axis) -> std::string_view;
[[nodiscard]] auto is_joint_rotation_axis(std::size_t axis) -> bool;

// Shared joint settings asset (KHR_physics_rigid_bodies physicsJoints entry).
// Data only: constraints are built from this in the Six-DOF constraint
// wrapper (see iconstraint.hpp) by the editor's Joint prim.
//
// The six degrees of freedom are eleven registered properties each (66 in
// all, doc/erhe/property_system.md section 4.22): one limit and one drive per
// axis, which is what both backends accept, what both file formats admit and
// what the simulation implements. The two arrays below are MIRRORS of the
// effective values, refreshed by on_property_changed, so an edit from any
// writer - a generic row, an undo, a file load, a value held by a folder or a
// style - reaches every reader.
class Physics_joint_settings : public erhe::Item<erhe::Item_base, erhe::Typed, Physics_joint_settings>
{
public:
    Physics_joint_settings();
    explicit Physics_joint_settings(std::string_view name);
    explicit Physics_joint_settings(const Physics_joint_settings&);
    Physics_joint_settings& operator=(const Physics_joint_settings&);
    ~Physics_joint_settings() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Physics_joint_settings"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Typed::get_static_type() | erhe::Item_type::physics_joint_settings; }

    // Overrides erhe::Typed: the class fixes the token. USD has no prim type
    // for this kind, so the token is the erhe class name, written as a custom
    // typeName (doc/erhe/usd_compatibility.md).
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Physics_joint_settings"; }

    // Implements erhe::property::Dependency_object: refreshes the mirrors.
    void on_property_changed(const erhe::property::Property_changed_args& args) override;

    // Registered properties, one element per axis (index 0..5 as above).
    // All of them serialize, are carried by both file formats' own joint
    // entry (native_gltf) and inherit: every one has a plain scalar default,
    // so a value supplied by a folder or a style is an ordinary opinion the
    // item's own local value overrides.
    static const std::array<erhe::property::Property<Joint_axis_limit>, c_joint_axis_count> limit_property;
    static const std::array<erhe::property::Property<float>,            c_joint_axis_count> limit_min_property;
    static const std::array<erhe::property::Property<float>,            c_joint_axis_count> limit_max_property;
    static const std::array<erhe::property::Property<float>,            c_joint_axis_count> limit_stiffness_property;
    static const std::array<erhe::property::Property<float>,            c_joint_axis_count> limit_damping_property;
    static const std::array<erhe::property::Property<Joint_axis_drive>, c_joint_axis_count> drive_property;
    static const std::array<erhe::property::Property<float>,            c_joint_axis_count> drive_max_force_property;
    static const std::array<erhe::property::Property<float>,            c_joint_axis_count> drive_position_target_property;
    static const std::array<erhe::property::Property<float>,            c_joint_axis_count> drive_velocity_target_property;
    static const std::array<erhe::property::Property<float>,            c_joint_axis_count> drive_stiffness_property;
    static const std::array<erhe::property::Property<float>,            c_joint_axis_count> drive_damping_property;

    // The mirrors the constraint is built from. Index 0..2 translation XYZ,
    // 3..5 rotation XYZ - the layout of Six_dof_constraint_settings.
    [[nodiscard]] auto get_axis_limits() const -> const std::array<Constraint_axis_limit, c_joint_axis_count>& { return m_limits; }
    [[nodiscard]] auto get_axis_drives() const -> const std::array<Constraint_axis_drive, c_joint_axis_count>& { return m_drives; }

    // Typed writers; every one of them writes the entry store, so the
    // mirrors follow. An unwritten limit_min / limit_max leaves that side of
    // the limit unbounded (value source `default`).
    void set_axis_limit                (std::size_t axis, Joint_axis_limit value) { set_value(limit_property                [axis], value); }
    void set_axis_limit_min            (std::size_t axis, float value)            { set_value(limit_min_property            [axis], value); }
    void set_axis_limit_max            (std::size_t axis, float value)            { set_value(limit_max_property            [axis], value); }
    void set_axis_limit_stiffness      (std::size_t axis, float value)            { set_value(limit_stiffness_property      [axis], value); }
    void set_axis_limit_damping        (std::size_t axis, float value)            { set_value(limit_damping_property        [axis], value); }
    void set_axis_drive                (std::size_t axis, Joint_axis_drive value) { set_value(drive_property                [axis], value); }
    void set_axis_drive_max_force      (std::size_t axis, float value)            { set_value(drive_max_force_property      [axis], value); }
    void set_axis_drive_position_target(std::size_t axis, float value)            { set_value(drive_position_target_property[axis], value); }
    void set_axis_drive_velocity_target(std::size_t axis, float value)            { set_value(drive_velocity_target_property[axis], value); }
    void set_axis_drive_stiffness      (std::size_t axis, float value)            { set_value(drive_stiffness_property      [axis], value); }
    void set_axis_drive_damping        (std::size_t axis, float value)            { set_value(drive_damping_property        [axis], value); }

    // True while the axis is limited to a single value (min == max), which is
    // how both formats and both backends spell a locked axis.
    [[nodiscard]] auto is_axis_fixed(std::size_t axis) const -> bool;

private:
    void refresh_mirrors();

    std::array<Constraint_axis_limit, c_joint_axis_count> m_limits{};
    std::array<Constraint_axis_drive, c_joint_axis_count> m_drives{};
    // Constraint_axis_drive has no acceleration mode, so an acceleration
    // drive mirrors as force and says so once per item.
    bool m_warned_acceleration_drive{false};
};

} // namespace erhe::physics
