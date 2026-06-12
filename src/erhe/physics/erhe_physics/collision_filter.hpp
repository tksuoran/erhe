#pragma once

#include "erhe_item/item.hpp"

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
// The Jolt backend compiles a filter once per item (keyed by item pointer);
// editing a live filter requires re-assigning it to the bodies that use it.
class Collision_filter : public erhe::Item<erhe::Item_base, erhe::Item_base, Collision_filter>
{
public:
    Collision_filter();
    explicit Collision_filter(std::string_view name);
    explicit Collision_filter(const Collision_filter&);
    Collision_filter& operator=(const Collision_filter&);
    ~Collision_filter() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Collision_filter"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::collision_filter; }

    std::vector<std::string> collision_systems;
    std::vector<std::string> collide_with_systems;     // non-empty => allowlist semantics
    std::vector<std::string> not_collide_with_systems; // used when collide_with_systems is empty => denylist semantics
};

} // namespace erhe::physics
