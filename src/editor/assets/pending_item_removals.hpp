#pragma once

#include "app_message.hpp"

#include <memory>
#include <vector>

namespace erhe { class Item_base; }

namespace editor {

// Collects the items that left a content library or an unregistered scene
// since the last flush, so the editor can announce them once per frame
// (Items_removed_message) instead of once per removal - undoing a large glTF
// import removes thousands of library entries, each from inside a held
// non-recursive mutex.
//
// note_attached() cancelling a pending note is what keeps the hook free of
// false positives: erhe::Hierarchy::set_parent() runs handle_remove_child()
// and then handle_add_child() in one call, and Hierarchy::remove() promotes a
// folder's children to the grandparent before orphaning itself, so a library
// folder move detaches and re-attaches within the same frame and cancels
// itself before the flush.
//
// Main thread only, like the rest of Asset_manager: every producer (operation
// execute / undo, library mutation, scene unregistration) already runs there,
// so this carries no mutex.
//
// See doc/import-undo-reference-clearing.md.
class Pending_item_removals
{
public:
    // Records an item as removed. Repeated notes for one item collapse into a
    // single announcement.
    void note_detached(const std::shared_ptr<erhe::Item_base>& item);

    // Cancels a pending note, if any. Cheap no-op for an item that was never
    // detached.
    void note_attached(const erhe::Item_base* item);

    [[nodiscard]] auto empty() const -> bool;

    // Moves the pending state out and resolves it into the message payload;
    // returns null when nothing is pending or when every noted item died
    // before the flush. Moving out BEFORE the caller dispatches is the
    // contract: a subscriber that notes another removal during the dispatch
    // must land in the next batch, not be discarded by a trailing clear.
    [[nodiscard]] auto take() -> std::shared_ptr<const Removed_items>;

private:
    // weak_ptr: an item that dies before the flush needs no announcement, and
    // this must not become a holder of an asset that is about to be unloaded
    // (Asset_manager::unload_record's exclusivity check would then report the
    // manager itself as an undeclared user).
    std::vector<std::weak_ptr<erhe::Item_base>> m_pending;
};

}
