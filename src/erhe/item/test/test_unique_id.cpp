#include "erhe_item/unique_id.hpp"

#include <gtest/gtest.h>

#include <type_traits>

namespace {

struct Tag_a {};
struct Tag_b {};

TEST(UniqueId, DefaultConstruction_NonZero)
{
    erhe::Unique_id<Tag_a> id;
    EXPECT_NE(id.get_id(), erhe::Unique_id<Tag_a>::null_id);
}

TEST(UniqueId, NullIdIsZero)
{
    EXPECT_EQ(erhe::Unique_id<Tag_a>::null_id, 0u);
}

TEST(UniqueId, SequentialIds)
{
    erhe::Unique_id<Tag_a> id1;
    erhe::Unique_id<Tag_a> id2;
    EXPECT_NE(id1.get_id(), id2.get_id());
    EXPECT_GT(id2.get_id(), id1.get_id());
}

TEST(UniqueId, NoCopy)
{
    static_assert(!std::is_copy_constructible_v<erhe::Unique_id<Tag_a>>);
    static_assert(!std::is_copy_assignable_v<erhe::Unique_id<Tag_a>>);
}

TEST(UniqueId, MoveConstructor)
{
    erhe::Unique_id<Tag_a> src;
    const auto original_id = src.get_id();
    EXPECT_NE(original_id, erhe::Unique_id<Tag_a>::null_id);

    erhe::Unique_id<Tag_a> dst{std::move(src)};
    EXPECT_EQ(dst.get_id(), original_id);
    EXPECT_EQ(src.get_id(), erhe::Unique_id<Tag_a>::null_id);
}

TEST(UniqueId, MoveAssignment)
{
    erhe::Unique_id<Tag_a> src;
    erhe::Unique_id<Tag_a> dst;
    const auto original_id = src.get_id();

    dst = std::move(src);
    EXPECT_EQ(dst.get_id(), original_id);
    EXPECT_EQ(src.get_id(), erhe::Unique_id<Tag_a>::null_id);
}

TEST(UniqueId, Reset)
{
    erhe::Unique_id<Tag_a> id;
    EXPECT_NE(id.get_id(), erhe::Unique_id<Tag_a>::null_id);
    id.reset();
    EXPECT_EQ(id.get_id(), erhe::Unique_id<Tag_a>::null_id);
}

TEST(UniqueId, DifferentTypesHaveIndependentCounters)
{
    // Both type instantiations have their own static counter.
    // We can only verify they produce valid non-zero IDs independently.
    erhe::Unique_id<Tag_a> a1;
    erhe::Unique_id<Tag_b> b1;
    EXPECT_NE(a1.get_id(), erhe::Unique_id<Tag_a>::null_id);
    EXPECT_NE(b1.get_id(), erhe::Unique_id<Tag_b>::null_id);
}

} // namespace
