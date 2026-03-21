#include "erhe_item/item.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace {

TEST(ItemFlags, NoneIsZero)
{
    EXPECT_EQ(erhe::Item_flags::none, 0u);
}

TEST(ItemFlags, DistinctBits)
{
    std::set<uint64_t> values;
    for (uint64_t i = 0; i < erhe::Item_flags::count; ++i) {
        const uint64_t bit = uint64_t{1} << i;
        EXPECT_TRUE(values.insert(bit).second) << "Duplicate bit at position " << i;
    }
}

TEST(ItemFlags, LabelCount)
{
    constexpr auto label_count = sizeof(erhe::Item_flags::c_bit_labels) / sizeof(erhe::Item_flags::c_bit_labels[0]);
    EXPECT_EQ(label_count, erhe::Item_flags::count);
}

TEST(ItemFlags, ToStringEmpty)
{
    EXPECT_TRUE(erhe::Item_flags::to_string(0).empty());
}

TEST(ItemFlags, ToStringSingleBit)
{
    const std::string result = erhe::Item_flags::to_string(erhe::Item_flags::selected);
    EXPECT_NE(result.find("Selected"), std::string::npos);
}

TEST(ItemFlags, ToStringMultipleBits)
{
    const std::string result = erhe::Item_flags::to_string(erhe::Item_flags::selected | erhe::Item_flags::visible);
    EXPECT_NE(result.find("Selected"), std::string::npos);
    EXPECT_NE(result.find("Visible"), std::string::npos);
    EXPECT_NE(result.find(" | "), std::string::npos);
}

TEST(ItemType, NoneIsZero)
{
    EXPECT_EQ(erhe::Item_type::none, 0u);
}

TEST(ItemType, DistinctBits)
{
    std::set<uint64_t> values;
    for (uint64_t i = 1; i <= erhe::Item_type::count; ++i) {
        const uint64_t bit = uint64_t{1} << i;
        EXPECT_TRUE(values.insert(bit).second) << "Duplicate type bit at index " << i;
    }
}

TEST(ItemType, LabelCount)
{
    // c_bit_labels has count entries: "none" at index 0, then indices 1..count-1
    constexpr auto label_count = sizeof(erhe::Item_type::c_bit_labels) / sizeof(erhe::Item_type::c_bit_labels[0]);
    EXPECT_EQ(label_count, erhe::Item_type::count);
}

} // namespace
