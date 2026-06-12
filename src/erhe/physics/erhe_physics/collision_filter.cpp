#include "erhe_physics/collision_filter.hpp"

namespace erhe::physics {

Collision_filter::Collision_filter()                                   = default;
Collision_filter::Collision_filter(const Collision_filter&)            = default;
Collision_filter& Collision_filter::operator=(const Collision_filter&) = default;
Collision_filter::~Collision_filter() noexcept                         = default;

Collision_filter::Collision_filter(const std::string_view name)
    : Item{name}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

} // namespace erhe::physics
