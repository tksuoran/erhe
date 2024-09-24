#include "erhe_item/item_host.hpp"
#include "erhe_item/item.hpp"

namespace erhe {

Item_host::~Item_host() noexcept = default;

auto resolve_item_host(const Item_base* a, const Item_base* b, const Item_base* c) -> Item_host*
{
    Item_host* host_a = (a != nullptr) ? a->get_item_host() : nullptr;
    Item_host* host_b = (b != nullptr) ? b->get_item_host() : nullptr;
    Item_host* host_c = (c != nullptr) ? c->get_item_host() : nullptr;
    return (host_a != nullptr) ? host_a : (host_b != nullptr) ? host_b : host_c;
}

Item_host_lock_guard::Item_host_lock_guard(const Item_base* a, const Item_base* b, const Item_base* c)
    : m_lock{resolve_item_host(a, b, c)->item_host_mutex}
{
}

} // namespace erhe

