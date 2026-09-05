#pragma once

#include "erhe_profile/profile.hpp"

#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace erhe {

class Item_base;

class Item_host
{
public:
    virtual ~Item_host() noexcept;

    [[nodiscard]] virtual auto get_host_name() const -> const char* = 0;

    // The hosted item a path or a name addresses, for expression
    // references (doc/property-system.md D22) and object references (D28);
    // nullptr when the host has no such item or does no lookup. A text
    // holding '/' is a path (Hierarchy::get_path()) and a text without one
    // is a name, so both the current and the older stored form resolve.
    // Scene_host walks the scene's node tree, then its nodes and
    // attachments by name; the editor's Scene_root adds the content
    // library, whose folder paths use the same form.
    [[nodiscard]] virtual auto find_hosted_item(std::string_view name_or_path) -> Item_base* { static_cast<void>(name_or_path); return nullptr; }

    ERHE_PROFILE_MUTEX(std::mutex, item_host_mutex);
    static ERHE_PROFILE_MUTEX_DECLARATION(std::mutex, orphan_item_host_mutex);

    // Per-host selection bucket: the items of the editor selection hosted by
    // this Item_host. Not continuously maintained - the editor's Selection
    // clears and refills it (capacity retained) each time a per-host view is
    // requested, deriving the contents from the authoritative selection at
    // query time so a host change while selected (e.g. a cross-scene
    // reparent) can never leave a stale bucket here. Always access through
    // Selection::get_hosted_selection().
    std::vector<std::shared_ptr<Item_base>> hosted_selection;
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
