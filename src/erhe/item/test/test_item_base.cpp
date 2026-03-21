#include "erhe_item/item.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

namespace {

class Concrete_item : public erhe::Item<erhe::Item_base, erhe::Item_base, Concrete_item>
{
public:
    using Item::Item;
    static constexpr std::string_view static_type_name{"Concrete_item"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::mesh; }
};

TEST(ItemBase, DefaultConstruction)
{
    auto item = std::make_shared<Concrete_item>();
    EXPECT_NE(item->get_id(), 0u);
    EXPECT_TRUE(item->get_name().empty());
    EXPECT_EQ(item->get_flag_bits(), erhe::Item_flags::none);
}

TEST(ItemBase, NamedConstruction)
{
    auto item = std::make_shared<Concrete_item>("hello");
    EXPECT_EQ(item->get_name(), "hello");
    EXPECT_NE(item->get_id(), 0u);
}

TEST(ItemBase, SetName)
{
    auto item = std::make_shared<Concrete_item>("old");
    item->set_name("new");
    EXPECT_EQ(item->get_name(), "new");
}

TEST(ItemBase, UniqueIds)
{
    auto item1 = std::make_shared<Concrete_item>();
    auto item2 = std::make_shared<Concrete_item>();
    EXPECT_NE(item1->get_id(), item2->get_id());
}

TEST(ItemBase, EnableFlagBits)
{
    auto item = std::make_shared<Concrete_item>();
    EXPECT_FALSE(item->is_visible());
    item->enable_flag_bits(erhe::Item_flags::visible);
    EXPECT_TRUE(item->is_visible());
}

TEST(ItemBase, DisableFlagBits)
{
    auto item = std::make_shared<Concrete_item>();
    item->enable_flag_bits(erhe::Item_flags::visible);
    EXPECT_TRUE(item->is_visible());
    item->disable_flag_bits(erhe::Item_flags::visible);
    EXPECT_FALSE(item->is_visible());
}

TEST(ItemBase, SetFlagBitsTrue)
{
    auto item = std::make_shared<Concrete_item>();
    item->set_flag_bits(erhe::Item_flags::selected, true);
    EXPECT_TRUE(item->is_selected());
}

TEST(ItemBase, SetFlagBitsFalse)
{
    auto item = std::make_shared<Concrete_item>();
    item->set_flag_bits(erhe::Item_flags::selected, true);
    item->set_flag_bits(erhe::Item_flags::selected, false);
    EXPECT_FALSE(item->is_selected());
}

TEST(ItemBase, SetSelected)
{
    auto item = std::make_shared<Concrete_item>();
    item->set_selected(true);
    EXPECT_TRUE(item->is_selected());
    item->set_selected(false);
    EXPECT_FALSE(item->is_selected());
}

TEST(ItemBase, ShowHide)
{
    auto item = std::make_shared<Concrete_item>();
    item->show();
    EXPECT_TRUE(item->is_visible());
    EXPECT_FALSE(item->is_hidden());
    item->hide();
    EXPECT_FALSE(item->is_visible());
    EXPECT_TRUE(item->is_hidden());
}

TEST(ItemBase, SetVisible)
{
    auto item = std::make_shared<Concrete_item>();
    item->set_visible(true);
    EXPECT_TRUE(item->is_visible());
    item->set_visible(false);
    EXPECT_FALSE(item->is_visible());
}

TEST(ItemBase, IsHovered)
{
    auto item = std::make_shared<Concrete_item>();
    EXPECT_FALSE(item->is_hovered());

    item->enable_flag_bits(erhe::Item_flags::hovered_in_viewport);
    EXPECT_TRUE(item->is_hovered());
    item->disable_flag_bits(erhe::Item_flags::hovered_in_viewport);
    EXPECT_FALSE(item->is_hovered());

    item->enable_flag_bits(erhe::Item_flags::hovered_in_item_tree);
    EXPECT_TRUE(item->is_hovered());
}

TEST(ItemBase, IsShownInUi)
{
    auto item = std::make_shared<Concrete_item>();
    EXPECT_FALSE(item->is_shown_in_ui());
    item->enable_flag_bits(erhe::Item_flags::show_in_ui);
    EXPECT_TRUE(item->is_shown_in_ui());
}

TEST(ItemBase, MultipleFlagBits)
{
    auto item = std::make_shared<Concrete_item>();
    item->enable_flag_bits(erhe::Item_flags::visible);
    item->enable_flag_bits(erhe::Item_flags::selected);
    item->enable_flag_bits(erhe::Item_flags::opaque);
    EXPECT_TRUE(item->is_visible());
    EXPECT_TRUE(item->is_selected());
    EXPECT_EQ(item->get_flag_bits() & erhe::Item_flags::opaque, erhe::Item_flags::opaque);

    item->disable_flag_bits(erhe::Item_flags::selected);
    EXPECT_TRUE(item->is_visible());
    EXPECT_FALSE(item->is_selected());
    EXPECT_EQ(item->get_flag_bits() & erhe::Item_flags::opaque, erhe::Item_flags::opaque);
}

TEST(ItemBase, CopyConstruction)
{
    auto original = std::make_shared<Concrete_item>("original");
    original->set_selected(true);
    original->show();

    auto copy = std::make_shared<Concrete_item>(*original);
    EXPECT_NE(copy->get_id(), original->get_id());
    EXPECT_EQ(copy->get_name(), "original Copy");
    EXPECT_FALSE(copy->is_selected()); // selected is stripped
    EXPECT_TRUE(copy->is_visible());   // other flags are preserved
}

TEST(ItemBase, SourcePath)
{
    auto item = std::make_shared<Concrete_item>();
    EXPECT_EQ(item->get_source_path(), nullptr);

    const std::filesystem::path p{"test/path.txt"};
    item->set_source_path(p);
    ASSERT_NE(item->get_source_path(), nullptr);
    EXPECT_EQ(*item->get_source_path(), p);
}

TEST(ItemBase, Describe)
{
    auto item = std::make_shared<Concrete_item>("test_item");
    item->show();

    const std::string d0 = item->describe(0);
    EXPECT_EQ(d0, "test_item");

    const std::string d1 = item->describe(1);
    EXPECT_NE(d1.find("Concrete_item"), std::string::npos);
    EXPECT_NE(d1.find("test_item"), std::string::npos);

    const std::string d2 = item->describe(2);
    EXPECT_NE(d2.find("id"), std::string::npos);

    const std::string d3 = item->describe(3);
    EXPECT_NE(d3.find("flags"), std::string::npos);
}

} // namespace
