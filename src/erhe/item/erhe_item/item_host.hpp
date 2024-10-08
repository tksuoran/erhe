#pragma once

#include "erhe_profile/profile.hpp"

#include <memory>
#include <mutex>

namespace erhe {

class Item_base;

class Item_host
{
public:
    virtual ~Item_host() noexcept;

    [[nodiscard]] virtual auto get_host_name() const -> const char* = 0;

    ERHE_PROFILE_MUTEX(std::mutex, item_host_mutex);
    static ERHE_PROFILE_MUTEX_DECLARATION(std::mutex, orphan_item_host_mutex);
};

auto resolve_item_host(const Item_base* a, const Item_base* b, const Item_base* c) -> Item_host*;

auto resolve_item_host_mutex(const Item_base* a, const Item_base* b, const Item_base* c) -> ERHE_PROFILE_LOCKABLE_BASE(std::mutex)&;

class Item_host_lock_guard
{
public:
    explicit Item_host_lock_guard(const Item_base* a, const Item_base* b = nullptr, const Item_base* c = nullptr);

private:
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> m_lock;
};

} // namespace erhe
