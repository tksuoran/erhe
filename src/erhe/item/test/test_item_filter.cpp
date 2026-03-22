#include "erhe_item/item.hpp"

#include <gtest/gtest.h>

namespace {

using erhe::Item_filter;
using erhe::Item_flags;

constexpr uint64_t A = Item_flags::visible;
constexpr uint64_t B = Item_flags::selected;
constexpr uint64_t C = Item_flags::opaque;

TEST(ItemFilter, DefaultPassesAll)
{
    Item_filter filter;
    EXPECT_TRUE(filter(0));
    EXPECT_TRUE(filter(A));
    EXPECT_TRUE(filter(A | B | C));
    EXPECT_TRUE(filter(~uint64_t{0}));
}

TEST(ItemFilter, RequireAllBitsSet_Pass)
{
    Item_filter filter;
    filter.require_all_bits_set = A | B;
    EXPECT_TRUE(filter(A | B));
    EXPECT_TRUE(filter(A | B | C));
}

TEST(ItemFilter, RequireAllBitsSet_Fail)
{
    Item_filter filter;
    filter.require_all_bits_set = A | B;
    EXPECT_FALSE(filter(A));
    EXPECT_FALSE(filter(B));
    EXPECT_FALSE(filter(0));
}

TEST(ItemFilter, RequireAtLeastOneBitSet_Pass)
{
    Item_filter filter;
    filter.require_at_least_one_bit_set = A | B;
    EXPECT_TRUE(filter(A));
    EXPECT_TRUE(filter(B));
    EXPECT_TRUE(filter(A | B));
    EXPECT_TRUE(filter(A | C));
}

TEST(ItemFilter, RequireAtLeastOneBitSet_Fail)
{
    Item_filter filter;
    filter.require_at_least_one_bit_set = A | B;
    EXPECT_FALSE(filter(0));
    EXPECT_FALSE(filter(C));
}

TEST(ItemFilter, RequireAtLeastOneBitSet_ZeroMaskIsVacuouslyTrue)
{
    Item_filter filter;
    filter.require_at_least_one_bit_set = 0;
    EXPECT_TRUE(filter(0));
    EXPECT_TRUE(filter(C));
}

TEST(ItemFilter, RequireAllBitsClear_Pass)
{
    Item_filter filter;
    filter.require_all_bits_clear = A | B;
    EXPECT_TRUE(filter(0));
    EXPECT_TRUE(filter(C));
}

TEST(ItemFilter, RequireAllBitsClear_Fail)
{
    Item_filter filter;
    filter.require_all_bits_clear = A | B;
    EXPECT_FALSE(filter(A));
    EXPECT_FALSE(filter(B));
    EXPECT_FALSE(filter(A | B));
}

TEST(ItemFilter, RequireAtLeastOneBitClear_Pass)
{
    Item_filter filter;
    filter.require_at_least_one_bit_clear = A | B;
    EXPECT_TRUE(filter(A));      // B is clear
    EXPECT_TRUE(filter(B));      // A is clear
    EXPECT_TRUE(filter(0));      // both clear
    EXPECT_TRUE(filter(A | C));  // B is clear
}

TEST(ItemFilter, RequireAtLeastOneBitClear_Fail)
{
    Item_filter filter;
    filter.require_at_least_one_bit_clear = A | B;
    EXPECT_FALSE(filter(A | B));
    EXPECT_FALSE(filter(A | B | C));
}

TEST(ItemFilter, RequireAtLeastOneBitClear_ZeroMaskIsVacuouslyTrue)
{
    Item_filter filter;
    filter.require_at_least_one_bit_clear = 0;
    EXPECT_TRUE(filter(A | B | C));
}

TEST(ItemFilter, CombinedConditions)
{
    Item_filter filter;
    filter.require_all_bits_set           = A;     // must have A
    filter.require_all_bits_clear         = C;     // must not have C
    filter.require_at_least_one_bit_clear = A | B; // A or B must be clear

    // A set, C clear, B clear -> pass (at least one of A|B is clear: B)
    EXPECT_TRUE(filter(A));

    // A set, B set, C clear -> fail (require_at_least_one_bit_clear: both A and B set)
    EXPECT_FALSE(filter(A | B));

    // A set, C set -> fail (require_all_bits_clear: C is set)
    EXPECT_FALSE(filter(A | C));
}

TEST(ItemFilter, DescribeNonDefault)
{
    Item_filter filter;
    filter.require_all_bits_set = A;
    const std::string desc = filter.describe();
    EXPECT_FALSE(desc.empty());
    EXPECT_NE(desc.find("require_all_bits_set"), std::string::npos);
}

TEST(ItemFilter, DescribeDefault)
{
    Item_filter filter;
    EXPECT_TRUE(filter.describe().empty());
}

} // namespace
