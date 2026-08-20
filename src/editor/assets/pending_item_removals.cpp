#include "assets/pending_item_removals.hpp"

#include "erhe_item/item.hpp"

#include <algorithm>

namespace editor {

void Pending_item_removals::note_detached(const std::shared_ptr<erhe::Item_base>& item)
{
    if (!item) {
        return;
    }
    const erhe::Item_base* const raw = item.get();
    const auto i = std::find_if(
        m_pending.begin(),
        m_pending.end(),
        [raw](const std::weak_ptr<erhe::Item_base>& entry) {
            const std::shared_ptr<erhe::Item_base> locked = entry.lock();
            return locked.get() == raw;
        }
    );
    if (i != m_pending.end()) {
        return; // already noted; one item is one announcement
    }
    m_pending.push_back(item);
}

void Pending_item_removals::note_attached(const erhe::Item_base* const item)
{
    if ((item == nullptr) || m_pending.empty()) {
        return;
    }
    const auto i = std::remove_if(
        m_pending.begin(),
        m_pending.end(),
        [item](const std::weak_ptr<erhe::Item_base>& entry) {
            const std::shared_ptr<erhe::Item_base> locked = entry.lock();
            // Expired entries are dropped here too: their item is gone, so
            // there is nothing left to announce.
            return !locked || (locked.get() == item);
        }
    );
    m_pending.erase(i, m_pending.end());
}

auto Pending_item_removals::empty() const -> bool
{
    return m_pending.empty();
}

auto Pending_item_removals::take() -> std::shared_ptr<const Removed_items>
{
    if (m_pending.empty()) {
        return {};
    }

    // Move out first: anything noted while the caller dispatches belongs to
    // the next batch, and must not be wiped by a trailing clear.
    const std::vector<std::weak_ptr<erhe::Item_base>> pending = std::move(m_pending);
    m_pending.clear();

    auto removed = std::make_shared<Removed_items>();
    removed->owners.reserve(pending.size());
    for (const std::weak_ptr<erhe::Item_base>& entry : pending) {
        std::shared_ptr<erhe::Item_base> item = entry.lock();
        if (!item) {
            continue; // died before the flush - nobody can be holding it
        }
        removed->lookup.insert(item.get());
        removed->owners.push_back(std::move(item));
    }
    if (removed->owners.empty()) {
        return {};
    }
    return removed;
}

}
