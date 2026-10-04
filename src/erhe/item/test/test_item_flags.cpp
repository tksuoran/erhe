#include "erhe_item/item.hpp"
#include "test_application_flags.hpp"

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

TEST(ItemFlags, LibraryBitsStayBelowTheApplicationRange)
{
    EXPECT_LE(erhe::Item_flags::count, erhe::Item_flags::application_first_bit);
    EXPECT_EQ(erhe::Item_flags::application_bit(0), uint64_t{1} << erhe::Item_flags::application_first_bit);
    EXPECT_NE(erhe::Item_flags::application_bit(0) & erhe::Item_flags::application_mask, 0u);
    EXPECT_EQ(erhe::Item_flags::lock_scale_z & erhe::Item_flags::application_mask, 0u);
}

TEST(ItemFlags, RegisteredApplicationLabelsAndMasks)
{
    // register_test_application_flags() ran from the test main.
    EXPECT_STREQ(erhe::Item_flags::label(erhe::Item_flags::application_first_bit + 0), "Tool");
    EXPECT_STREQ(erhe::Item_flags::label(0), erhe::Item_flags::c_bit_labels[0]);
    EXPECT_EQ(erhe::Item_flags::label(erhe::Item_flags::application_first_bit + 20), nullptr);
    EXPECT_EQ(erhe::Item_flags::get_transient_bits(), erhe::Item_flags::transient | Test_item_flags::transient);
    EXPECT_EQ(erhe::Item_flags::get_purpose_guide_when_set_bits(), Test_item_flags::purpose_guide_when_set);
    EXPECT_EQ(erhe::Item_flags::get_purpose_inputs(), Test_item_flags::purpose_guide_when_set | erhe::Item_flags::show_in_ui);
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
    const std::string result = erhe::Item_flags::to_string(erhe::Item_flags::selected | erhe::Item_flags::visible | Test_item_flags::tool);
    EXPECT_NE(result.find("Selected"), std::string::npos);
    EXPECT_NE(result.find("Visible"), std::string::npos);
    EXPECT_NE(result.find("Tool"), std::string::npos);
    EXPECT_NE(result.find(" | "), std::string::npos);
}

TEST(ItemType, NoneIsZero)
{
    EXPECT_EQ(erhe::Item_type::none, 0u);
}

TEST(ItemType, DistinctBits)
{
    std::set<uint64_t> values;
    for (uint64_t i = erhe::Item_type::library_first_index; i < erhe::Item_type::library_end_index; ++i) {
        const uint64_t bit = uint64_t{1} << i;
        EXPECT_TRUE(values.insert(bit).second) << "Duplicate type bit at index " << i;
    }
}

TEST(ItemType, LabelCount)
{
    constexpr auto label_count = sizeof(erhe::Item_type::c_bit_labels) / sizeof(erhe::Item_type::c_bit_labels[0]);
    EXPECT_EQ(label_count, erhe::Item_type::library_count);
}

TEST(ItemType, ApplicationRangeIsBelowTheLibraryRange)
{
    // The icon of the lowest set type bit wins, and an application class is
    // the more specific one.
    EXPECT_EQ(erhe::Item_type::application_index(0), 1u);
    EXPECT_LT(erhe::Item_type::application_index(erhe::Item_type::application_index_count - 1), erhe::Item_type::library_first_index);
    EXPECT_NE(erhe::Item_type::application_bit(3) & erhe::Item_type::application_mask, 0u);
    EXPECT_EQ(erhe::Item_type::mesh & erhe::Item_type::application_mask, 0u);
    EXPECT_STREQ(erhe::Item_type::label(erhe::Item_type::index_mesh), "Mesh");
    EXPECT_EQ(erhe::Item_type::label(0), nullptr);
}

} // namespace
