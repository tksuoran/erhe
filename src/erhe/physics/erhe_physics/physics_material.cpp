#include "erhe_physics/physics_material.hpp"

#include <algorithm>

namespace erhe::physics {

auto combine(const Combine_mode a, const Combine_mode b) -> Combine_mode
{
    // Spec precedence: average > minimum > maximum > multiply.
    // Combine_mode values are ordered so that the higher-precedence mode has
    // the lower integer value, so the pair mode is simply the minimum.
    return static_cast<Combine_mode>(std::min(static_cast<int>(a), static_cast<int>(b)));
}

auto combine_values(const Combine_mode mode, const float a, const float b) -> float
{
    switch (mode) {
        case Combine_mode::e_average:  return 0.5f * (a + b);
        case Combine_mode::e_minimum:  return std::min(a, b);
        case Combine_mode::e_maximum:  return std::max(a, b);
        case Combine_mode::e_multiply: return a * b;
        default:                       return 0.5f * (a + b);
    }
}

Physics_material::Physics_material()                                   = default;
Physics_material::Physics_material(const Physics_material&)            = default;
Physics_material& Physics_material::operator=(const Physics_material&) = default;
Physics_material::~Physics_material() noexcept                         = default;

Physics_material::Physics_material(const std::string_view name)
    : Item{name}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

} // namespace erhe::physics
