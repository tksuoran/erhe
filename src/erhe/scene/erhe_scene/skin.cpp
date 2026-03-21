#include "erhe_scene/skin.hpp"
#include "erhe_utility/bit_helpers.hpp"

namespace erhe::scene {

Skin::Skin()                       = default;
Skin::Skin(const Skin&)            = default;
Skin& Skin::operator=(const Skin&) = default;
Skin::~Skin() noexcept             = default;

Skin::Skin(const std::string_view name)
    : Item{name}
{
}

auto operator<(const Skin& lhs, const Skin& rhs) -> bool
{
    return lhs.get_id() < rhs.get_id();
}

using namespace erhe::utility;

auto is_bone(const Item_base* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return test_bit_set(item->get_type(), Item_type::bone);
}

auto is_bone(const std::shared_ptr<Item_base>& item) -> bool
{
    return is_bone(item.get());
}

} // namespace erhe::scene

