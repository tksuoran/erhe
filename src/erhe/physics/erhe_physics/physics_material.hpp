#pragma once

#include "erhe_item/item.hpp"

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

// Resolves the combine mode for a contact pair per KHR_physics_rigid_bodies
// precedence: if either material uses average the result is average; else if
// either uses minimum the result is minimum; else if either uses maximum the
// result is maximum; else multiply.
[[nodiscard]] auto combine(Combine_mode a, Combine_mode b) -> Combine_mode;

// Combines two scalar material values (friction or restitution) using the
// given combine mode.
[[nodiscard]] auto combine_values(Combine_mode mode, float a, float b) -> float;

// Shared physics material asset (KHR_physics_rigid_bodies physicsMaterials
// entry). Referenced by rigid bodies via IRigid_body_create_info /
// IRigid_body::set_physics_material(). The Jolt backend snapshots the values
// into a POD per body; editing a live material requires re-assigning it to
// the bodies that use it.
class Physics_material : public erhe::Item<erhe::Item_base, erhe::Item_base, Physics_material>
{
public:
    Physics_material();
    explicit Physics_material(std::string_view name);
    explicit Physics_material(const Physics_material&);
    Physics_material& operator=(const Physics_material&);
    ~Physics_material() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Physics_material"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::physics_material; }

    float        static_friction    {0.6f};
    float        dynamic_friction   {0.6f};
    float        restitution        {0.0f};
    Combine_mode friction_combine   {Combine_mode::e_average};
    Combine_mode restitution_combine{Combine_mode::e_average};
};

} // namespace erhe::physics
