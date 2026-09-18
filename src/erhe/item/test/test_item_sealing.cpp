// Item_flags::lock_edit is the property-store seal (D24 in
// doc/erhe/property_system.md): every writer of the flag seals through
// set_flag_bits, and copies re-derive the seal from the copied flag.

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace {

class Locked_item : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Locked_item>
{
public:
    explicit Locked_item(const std::string_view name) : Item{name} {}
    explicit Locked_item(const Locked_item& other) = default;
    static constexpr std::string_view static_type_name{"Locked_item"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return uint64_t{1} << 58; }
};

} // namespace

TEST(Item_sealing, lock_edit_seals_and_unseals)
{
    auto item = std::make_shared<Locked_item>("i");
    EXPECT_FALSE(item->is_sealed());

    item->set_lock_edit(true);
    EXPECT_TRUE(item->is_lock_edit());
    EXPECT_TRUE(item->is_sealed());
    EXPECT_FALSE(item->set_value(erhe::Item_base::visible_property.get(), erhe::property::Property_value{false}));
    EXPECT_TRUE(item->is_visible());

    item->set_lock_edit(false);
    EXPECT_FALSE(item->is_sealed());
    EXPECT_TRUE(item->set_value(erhe::Item_base::visible_property.get(), erhe::property::Property_value{false}));
    EXPECT_FALSE(item->is_visible());

    item->enable_flag_bits(erhe::Item_flags::lock_edit | erhe::Item_flags::lock_viewport_selection);
    EXPECT_TRUE(item->is_sealed());
    item->disable_flag_bits(erhe::Item_flags::lock_viewport_selection); // lock_edit untouched
    EXPECT_TRUE(item->is_sealed());
    item->set_flag_bits(erhe::Item_flags::lock_edit, false);
    EXPECT_FALSE(item->is_sealed());
}

TEST(Item_sealing, inherited_values_still_reach_a_sealed_child)
{
    auto parent = std::make_shared<Locked_item>("parent");
    auto child  = std::make_shared<Locked_item>("child");
    child->set_parent(parent);
    child->set_lock_edit(true);

    parent->hide();
    EXPECT_FALSE(child->is_visible());
    parent->show();
    EXPECT_TRUE(child->is_visible());
}

TEST(Item_sealing, copy_follows_the_copied_flag)
{
    auto locked = std::make_shared<Locked_item>("locked");
    locked->set_lock_edit(true);
    auto locked_copy = std::make_shared<Locked_item>(*locked);
    EXPECT_TRUE(locked_copy->is_lock_edit());
    EXPECT_TRUE(locked_copy->is_sealed());

    auto open = std::make_shared<Locked_item>("open");
    auto open_copy = std::make_shared<Locked_item>(*open);
    EXPECT_FALSE(open_copy->is_sealed());

    *locked_copy = *open; // assignment copies the flags too
    EXPECT_FALSE(locked_copy->is_lock_edit());
    EXPECT_FALSE(locked_copy->is_sealed());
}

// lock_edit_property is the seal's own switch (Property_flags::
// writable_when_sealed): the only property write a sealed item accepts.
TEST(Item_sealing, lock_edit_property_lifts_the_seal)
{
    auto item = std::make_shared<Locked_item>("i");
    EXPECT_TRUE(item->set_value(erhe::Item_base::lock_edit_property.get(), erhe::property::Property_value{true}));
    EXPECT_TRUE(item->is_lock_edit());
    EXPECT_TRUE(item->is_sealed());
    EXPECT_TRUE (item->is_write_sealed(erhe::Item_base::name_property.get()));
    EXPECT_FALSE(item->is_write_sealed(erhe::Item_base::lock_edit_property.get()));
    EXPECT_FALSE(item->set_value(erhe::Item_base::name_property.get(), erhe::property::Property_value{std::string{"renamed"}}));
    EXPECT_EQ(item->get_name(), "i");

    EXPECT_TRUE(item->set_value(erhe::Item_base::lock_edit_property.get(), erhe::property::Property_value{false}));
    EXPECT_FALSE(item->is_sealed());
    EXPECT_FALSE(item->is_write_sealed(erhe::Item_base::name_property.get()));
    EXPECT_TRUE(item->set_value(erhe::Item_base::name_property.get(), erhe::property::Property_value{std::string{"renamed"}}));
    EXPECT_EQ(item->get_name(), "renamed");
}
