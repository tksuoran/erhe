#pragma once

#include "erhe_item/item.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_property/dependency_property.hpp"

#include <string_view>

namespace erhe::physics {

// KHR_physics_rigid_bodies material property combine mode.
// The enum values encode the spec combine precedence: when two materials
// disagree, the mode with the LOWER value wins (average > minimum > maximum
// > multiply). See combine().
enum class Combine_mode : int {
    e_average  = 0,
    e_minimum  = 1,
    e_maximum  = 2,
    e_multiply = 3
};

extern const erhe::property::Enum_info c_combine_mode_enum_info;

// Resolves the combine mode for a contact pair per KHR_physics_rigid_bodies
// precedence: if either material uses average the result is average; else if
// either uses minimum the result is minimum; else if either uses maximum the
// result is maximum; else multiply.
[[nodiscard]] auto combine(Combine_mode a, Combine_mode b) -> Combine_mode;

// Combines two scalar material values (friction or restitution) using the
// given combine mode.
[[nodiscard]] auto combine_values(Combine_mode mode, float a, float b) -> float;

// The material defaults: the property defaults of Physics_material, and
// what a body with no material behaves like. Friction and restitution are
// the KHR_physics_rigid_bodies defaults; damping, wind receptivity and
// density are erhe's.
constexpr float c_default_friction         = 0.6f;
constexpr float c_default_restitution      = 0.0f;
constexpr float c_default_linear_damping   = 0.05f;
constexpr float c_default_angular_damping  = 0.05f;
constexpr float c_default_wind_receptivity = 0.0f;
constexpr float c_default_density          = 1.0f;

// Shared physics material asset (KHR_physics_rigid_bodies physicsMaterials
// entry), an item of the editor's content library (its Physics Materials
// category). It describes how a kind of matter behaves and is the only
// carrier of friction, restitution, linear and angular damping, wind
// receptivity and density: a rigid body references one through
// IRigid_body_create_info / IRigid_body::set_physics_material(), and a
// body without a material behaves like one with the defaults. The body
// keeps only what describes its own instance (mass, motion mode, initial
// velocities, ...); its default mass is its shape's volume times the
// material density. A backend may snapshot the values per body at
// set_physics_material(); the holder of the reference (the editor's
// Node_physics) observes the material's properties and pushes it to the
// body again on a change (doc/property-system.md section 4.12).
class Physics_material : public erhe::Item<erhe::Item_base, erhe::Typed, Physics_material>
{
public:
    Physics_material();
    explicit Physics_material(std::string_view name);
    explicit Physics_material(const Physics_material&);
    Physics_material& operator=(const Physics_material&);
    ~Physics_material() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Physics_material"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Typed::get_static_type() | erhe::Item_type::physics_material; }

    // Overrides erhe::Typed: the class fixes the token. USD has no prim type
    // for this kind, so the token is the erhe class name, written as a custom
    // typeName (doc/usd_compatibility.md).
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Physics_material"; }

    // Registered properties (erhe::property, doc/property-system.md
    // section 4.12): entry-stored, the spec defaults as property defaults.
    static const erhe::property::Property<float>        static_friction_property;
    static const erhe::property::Property<float>        dynamic_friction_property;
    static const erhe::property::Property<float>        restitution_property;
    static const erhe::property::Property<Combine_mode> friction_combine_property;
    static const erhe::property::Property<Combine_mode> restitution_combine_property;
    static const erhe::property::Property<float>        linear_damping_property;
    static const erhe::property::Property<float>        angular_damping_property;
    static const erhe::property::Property<float>        wind_receptivity_property;
    static const erhe::property::Property<float>        density_property;

    [[nodiscard]] auto get_static_friction    () const -> float        { return get_value(static_friction_property); }
    [[nodiscard]] auto get_dynamic_friction   () const -> float        { return get_value(dynamic_friction_property); }
    [[nodiscard]] auto get_restitution        () const -> float        { return get_value(restitution_property); }
    [[nodiscard]] auto get_friction_combine   () const -> Combine_mode { return get_value(friction_combine_property); }
    [[nodiscard]] auto get_restitution_combine() const -> Combine_mode { return get_value(restitution_combine_property); }
    [[nodiscard]] auto get_linear_damping     () const -> float        { return get_value(linear_damping_property); }
    [[nodiscard]] auto get_angular_damping    () const -> float        { return get_value(angular_damping_property); }
    // kg/s: the scene wind applies force = wind_receptivity * (wind velocity
    // - body velocity) at the center of mass each fixed step; 0 = unaffected
    // (the body is never touched, so it can sleep normally).
    [[nodiscard]] auto get_wind_receptivity   () const -> float        { return get_value(wind_receptivity_property); }
    // Mass per shape volume: the mass of a body without an explicit mass is
    // its shape volume times the density.
    [[nodiscard]] auto get_density            () const -> float        { return get_value(density_property); }

    void set_static_friction    (float value)        { set_value(static_friction_property, value); }
    void set_dynamic_friction   (float value)        { set_value(dynamic_friction_property, value); }
    void set_restitution        (float value)        { set_value(restitution_property, value); }
    void set_friction_combine   (Combine_mode value) { set_value(friction_combine_property, value); }
    void set_restitution_combine(Combine_mode value) { set_value(restitution_combine_property, value); }
    void set_linear_damping     (float value)        { set_value(linear_damping_property, value); }
    void set_angular_damping    (float value)        { set_value(angular_damping_property, value); }
    void set_wind_receptivity   (float value)        { set_value(wind_receptivity_property, value); }
    void set_density            (float value)        { set_value(density_property, value); }
};

} // namespace erhe::physics
