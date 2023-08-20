#include "erhe_scene/skin.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_item/unique_id.hpp"

namespace erhe::scene
{


Skin::Skin()
{
}

Skin::Skin(const std::string_view name)
    : Item{name, erhe::Unique_id<Skin>{}.get_id()}
{
}

Skin::~Skin() noexcept
{
}

auto Skin::get_static_type() -> uint64_t
{
    return Item_type::node_attachment | Item_type::skin;
}

auto Skin::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Skin::get_type_name() const -> std::string_view
{
    return static_type_name;
}

auto operator<(const Skin& lhs, const Skin& rhs) -> bool
{
    return lhs.get_id() < rhs.get_id();
}

using namespace erhe::bit;

auto is_skin(const Item* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return test_all_rhs_bits_set(item->get_type(), Item_type::skin);
}

auto is_skin(const std::shared_ptr<Item>& item) -> bool
{
    return is_skin(item.get());
}

auto is_bone(const Item* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return test_all_rhs_bits_set(item->get_type(), Item_type::bone);
}

auto is_bone(const std::shared_ptr<Item>& item) -> bool
{
    return is_bone(item.get());
}

} // namespace erhe::scene

