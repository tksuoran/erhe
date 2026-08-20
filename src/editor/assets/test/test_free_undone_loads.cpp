// free_undone_loads picks which undone load to release.
//
// The index order is what makes the rule non-obvious: Operation_stack::undo()
// pushes the MOST RECENTLY recorded entry first, so in the redo stack index 0
// is the newest and back() is the entry that would be redone first. Entries
// recorded after index i sit at indices < i, so releasing index i requires
// discarding [0, i) - and releasing the HIGHEST index frees the most, because
// destroying those discarded entries releases their payloads too.
//
// See doc/reloadable-asset-loads.md.

#include "operations/operation_stack_selection.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

using editor::select_free_undone_loads_target;

TEST(free_undone_loads, empty_stack_selects_nothing)
{
    EXPECT_FALSE(select_free_undone_loads_target({}).has_value());
}

TEST(free_undone_loads, no_payload_selects_nothing)
{
    EXPECT_FALSE(select_free_undone_loads_target({false, false, false}).has_value());
}

TEST(free_undone_loads, single_payload_is_selected)
{
    const std::optional<std::size_t> target = select_free_undone_loads_target({false, true, false});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target.value(), 1u);
}

// The earliest-recorded load is at the HIGHEST index. Selecting it frees the
// most, since everything recorded after it is discarded along the way.
TEST(free_undone_loads, highest_index_wins)
{
    const std::optional<std::size_t> target = select_free_undone_loads_target({true, false, true, false, true});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target.value(), 4u);
}

TEST(free_undone_loads, payload_at_the_back_is_selected)
{
    const std::optional<std::size_t> target = select_free_undone_loads_target({false, false, true});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target.value(), 2u);
}

TEST(free_undone_loads, payload_at_the_front_is_selected_when_alone)
{
    const std::optional<std::size_t> target = select_free_undone_loads_target({true, false, false});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target.value(), 0u);
}

// The selected index is what gets released; [0, i) is what gets discarded. A
// selection of 0 therefore discards nothing, which is the lossless case the
// automatic drop handles on its own.
TEST(free_undone_loads, selecting_index_zero_discards_nothing)
{
    const std::optional<std::size_t> target = select_free_undone_loads_target({true});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target.value(), 0u);
}

} // anonymous namespace
