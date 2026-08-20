// Pending_item_removals is the bookkeeping behind Items_removed_message: it
// collects the items that left a content library or an unregistered scene, so
// the editor can announce them once per frame instead of once per removal.
//
// See doc/import-undo-reference-clearing.md.

#include "assets/pending_item_removals.hpp"

#include "erhe_item/item.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace {

using editor::Pending_item_removals;
using editor::Removed_items;

[[nodiscard]] auto make_item(const std::string_view name) -> std::shared_ptr<erhe::Item_base>
{
    return std::make_shared<erhe::Item_base>(name);
}

TEST(pending_item_removals, detached_item_is_announced)
{
    Pending_item_removals pending;
    const std::shared_ptr<erhe::Item_base> item = make_item("item");

    pending.note_detached(item);
    EXPECT_FALSE(pending.empty());

    const std::shared_ptr<const Removed_items> removed = pending.take();
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed->owners.size(), 1u);
    EXPECT_TRUE(removed->lookup.contains(item.get()));
}

// A library folder move is a detach immediately followed by an attach, within
// one erhe::Hierarchy::set_parent() call. It must announce nothing.
TEST(pending_item_removals, reattached_item_is_not_announced)
{
    Pending_item_removals pending;
    const std::shared_ptr<erhe::Item_base> item = make_item("moved");

    pending.note_detached(item);
    pending.note_attached(item.get());

    EXPECT_TRUE(pending.empty());
    EXPECT_FALSE(pending.take());
}

// Asserting on `lookup` alone would be tautological - it is a set and cannot
// hold a duplicate. `owners` is a vector, and holds two entries unless
// note_detached() actually dedups.
TEST(pending_item_removals, repeated_detach_announces_once)
{
    Pending_item_removals pending;
    const std::shared_ptr<erhe::Item_base> item = make_item("item");

    pending.note_detached(item);
    pending.note_detached(item);

    const std::shared_ptr<const Removed_items> removed = pending.take();
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed->owners.size(), 1u);
    EXPECT_EQ(removed->lookup.size(), 1u);
}

TEST(pending_item_removals, attach_without_detach_is_harmless)
{
    Pending_item_removals pending;
    const std::shared_ptr<erhe::Item_base> item = make_item("never detached");

    pending.note_attached(item.get());
    pending.note_attached(nullptr);

    EXPECT_TRUE(pending.empty());
    EXPECT_FALSE(pending.take());
}

// weak_ptr contract: an item that dies before the flush needs no announcement
// (nobody can still be holding it), and the pending list must not have kept it
// alive in the meantime.
TEST(pending_item_removals, item_that_died_before_take_is_not_announced)
{
    Pending_item_removals pending;
    std::weak_ptr<erhe::Item_base> weak;
    {
        const std::shared_ptr<erhe::Item_base> item = make_item("short lived");
        weak = item;
        pending.note_detached(item);
        EXPECT_FALSE(pending.empty());
    }
    EXPECT_TRUE(weak.expired()); // the pending list did not pin it

    EXPECT_FALSE(pending.take());
    EXPECT_TRUE(weak.expired()); // ... and take() did not resurrect it
}

TEST(pending_item_removals, take_empties_and_untouched_take_is_null)
{
    Pending_item_removals pending;
    EXPECT_TRUE(pending.empty());
    EXPECT_FALSE(pending.take());

    const std::shared_ptr<erhe::Item_base> item = make_item("item");
    pending.note_detached(item);
    ASSERT_TRUE(pending.take());

    EXPECT_TRUE(pending.empty());
    EXPECT_FALSE(pending.take());
}

// take() moves the state out BEFORE the caller dispatches, so a note made
// during that dispatch belongs to the next batch instead of being wiped by a
// trailing clear. This pins the FIFO ordering that makes that safe; it does
// not itself exercise a re-entrant subscriber (take() sends nothing).
TEST(pending_item_removals, detach_after_take_is_announced_by_the_next_take)
{
    Pending_item_removals pending;
    const std::shared_ptr<erhe::Item_base> first  = make_item("first");
    const std::shared_ptr<erhe::Item_base> second = make_item("second");

    pending.note_detached(first);
    const std::shared_ptr<const Removed_items> first_batch = pending.take();
    ASSERT_TRUE(first_batch);

    pending.note_detached(second);
    const std::shared_ptr<const Removed_items> second_batch = pending.take();
    ASSERT_TRUE(second_batch);
    EXPECT_EQ(second_batch->owners.size(), 1u);
    EXPECT_TRUE (second_batch->lookup.contains(second.get()));
    EXPECT_FALSE(second_batch->lookup.contains(first.get()));
}

TEST(pending_item_removals, mixed_batch_announces_only_the_removed)
{
    Pending_item_removals pending;
    const std::shared_ptr<erhe::Item_base> a = make_item("a");
    const std::shared_ptr<erhe::Item_base> b = make_item("b");
    const std::shared_ptr<erhe::Item_base> c = make_item("c");

    pending.note_detached(a);
    pending.note_detached(b);
    pending.note_detached(c);
    pending.note_attached(b.get()); // b came back (a move)

    const std::shared_ptr<const Removed_items> removed = pending.take();
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed->owners.size(), 2u);
    EXPECT_TRUE (removed->lookup.contains(a.get()));
    EXPECT_FALSE(removed->lookup.contains(b.get()));
    EXPECT_TRUE (removed->lookup.contains(c.get()));
}

TEST(pending_item_removals, payload_invariants)
{
    Pending_item_removals pending;
    const std::shared_ptr<erhe::Item_base> a = make_item("a");
    const std::shared_ptr<erhe::Item_base> b = make_item("b");
    pending.note_detached(a);
    pending.note_detached(b);

    const std::shared_ptr<const Removed_items> removed = pending.take();
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed->owners.size(), removed->lookup.size());
    for (const std::shared_ptr<erhe::Item_base>& owner : removed->owners) {
        EXPECT_TRUE(removed->lookup.contains(owner.get()));
    }
}

TEST(pending_item_removals, attach_after_take_does_not_alter_the_taken_batch)
{
    Pending_item_removals pending;
    const std::shared_ptr<erhe::Item_base> item = make_item("item");
    pending.note_detached(item);

    const std::shared_ptr<const Removed_items> removed = pending.take();
    ASSERT_TRUE(removed);

    pending.note_attached(item.get()); // re-attached after the announcement

    EXPECT_EQ(removed->owners.size(), 1u);
    EXPECT_TRUE(removed->lookup.contains(item.get()));
}

} // anonymous namespace
