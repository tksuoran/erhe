#include "erhe/scene/skin.hpp"

#include "erhe/scene/scene_host.hpp"
#include "erhe/toolkit/bit_helpers.hpp"

namespace erhe::scene
{


Skin::Skin()
{
}

Skin::Skin(const std::string_view name)
    : Item{name}
{
}

Skin::~Skin() noexcept
{
}

auto Skin::get_static_type() -> uint64_t
{
    return Item_type::node_attachment | Item_type::skin;
}

auto Skin::get_static_type_name() -> const char*
{
    return "Skin";
}

auto Skin::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Skin::get_type_name() const -> const char*
{
    return get_static_type_name();
}

auto operator<(const Skin& lhs, const Skin& rhs) -> bool
{
    return lhs.m_id.get_id() < rhs.m_id.get_id();
}

using namespace erhe::toolkit;

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

auto as_skin(Item* const item) -> Skin*
{
    if (item == nullptr) {
        return nullptr;
    }
    if (!test_all_rhs_bits_set(item->get_type(), Item_type::skin)) {
        return nullptr;
    }
    return static_cast<Skin*>(item);
}

auto as_skin(const std::shared_ptr<Item>& item) -> std::shared_ptr<Skin>
{
    if (!item) {
        return {};
    }
    if (!test_all_rhs_bits_set(item->get_type(), Item_type::skin)) {
        return {};
    }
    return std::static_pointer_cast<Skin>(item);
}

//

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

