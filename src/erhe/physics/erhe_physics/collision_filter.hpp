#pragma once

#include "erhe_item/item.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_property/dependency_property.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace erhe::physics {

// Shared collision filter asset (KHR_physics_rigid_bodies collisionFilters
// entry). Collision systems are free-form strings; the Jolt backend interns
// them into 64-bit bitsets (at most 64 distinct system names per world).
//
// Semantics:
// - collision_systems: the systems this filter's body belongs to.
// - collide_with_systems non-empty: allowlist - the body collides only with
//   bodies that belong to at least one of the listed systems.
// - otherwise not_collide_with_systems is a denylist - the body collides with
//   everything except bodies that belong to one of the listed systems.
// The test is applied bidirectionally: both bodies' filters must allow the
// pair for a collision to occur.
//
// The Jolt backend compiles a filter once per item (keyed by item pointer)
// and recompiles it when the filter is assigned to a body again; the
// editor's Node_physics observes the filter's properties and re-assigns on
// every change (doc/erhe/property_system.md section 4.21), so an edit of a
// live filter reaches the simulation on its own.
class Collision_filter : public erhe::Item<erhe::Item_base, erhe::Typed, Collision_filter>
{
public:
    Collision_filter();
    explicit Collision_filter(std::string_view name);
    explicit Collision_filter(const Collision_filter&);
    Collision_filter& operator=(const Collision_filter&);
    ~Collision_filter() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Collision_filter"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Typed::get_static_type() | erhe::Item_type::collision_filter; }

    // Overrides erhe::Typed: the class fixes the token. USD has no prim type
    // for this kind, so the token is the erhe class name, written as a custom
    // typeName (doc/erhe/usd_compatibility.md).
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Collision_filter"; }

    // Implements erhe::property::Dependency_object: the three lists are
    // entry-stored properties, so the mirrors below follow every source of a
    // change (doc/erhe/property_system.md section 4.21).
    void on_property_changed(const erhe::property::Property_changed_args& args) override;

    // Registered properties (doc/erhe/property_system.md section 4.21): the
    // three system name lists, entry-stored `string[]` whose element count is
    // the user's. A filter states its own systems, so none of them inherits.
    static const erhe::property::Property<std::vector<std::string>> collision_systems_property;
    static const erhe::property::Property<std::vector<std::string>> collide_with_systems_property;
    static const erhe::property::Property<std::vector<std::string>> not_collide_with_systems_property;

    [[nodiscard]] auto get_collision_systems       () const -> const std::vector<std::string>& { return m_collision_systems; }
    [[nodiscard]] auto get_collide_with_systems    () const -> const std::vector<std::string>& { return m_collide_with_systems; }
    [[nodiscard]] auto get_not_collide_with_systems() const -> const std::vector<std::string>& { return m_not_collide_with_systems; }

    void set_collision_systems       (const std::vector<std::string>& value) { set_value(collision_systems_property,        value); }
    void set_collide_with_systems    (const std::vector<std::string>& value) { set_value(collide_with_systems_property,     value); }
    void set_not_collide_with_systems(const std::vector<std::string>& value) { set_value(not_collide_with_systems_property, value); }

private:
    // Mirrors of the effective values, refreshed by on_property_changed; the
    // backends read these while compiling a filter.
    std::vector<std::string> m_collision_systems;
    std::vector<std::string> m_collide_with_systems;     // non-empty => allowlist semantics
    std::vector<std::string> m_not_collide_with_systems; // used when collide_with_systems is empty => denylist semantics
};

} // namespace erhe::physics
