#include "erhe_item/item_host.hpp"
#include "erhe_item/item.hpp"

namespace erhe {

Item_host::~Item_host() noexcept = default;

ERHE_PROFILE_MUTEX(std::mutex, Item_host::orphan_item_host_mutex);

auto resolve_item_host(const Item_base* a, const Item_base* b, const Item_base* c) -> Item_host*
{
    Item_host* host_a = (a != nullptr) ? a->get_item_host() : nullptr;
    Item_host* host_b = (b != nullptr) ? b->get_item_host() : nullptr;
    Item_host* host_c = (c != nullptr) ? c->get_item_host() : nullptr;
    return (host_a != nullptr) ? host_a : (host_b != nullptr) ? host_b : host_c;
}

auto resolve_item_host_mutex(const Item_base* a, const Item_base* b, const Item_base* c) -> ERHE_PROFILE_LOCKABLE_BASE(std::mutex)&
{
    Item_host* host = resolve_item_host(a, b, c);
    return (host != nullptr) ? host->item_host_mutex : Item_host::orphan_item_host_mutex;
 }

Item_host_lock_guard::Item_host_lock_guard(const Item_base* a, const Item_base* b, const Item_base* c)
    : m_lock{
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)>{
            resolve_item_host_mutex(a, b, c)
        }
    }
{
}

} // namespace erhe

