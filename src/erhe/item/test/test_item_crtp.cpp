#include "erhe_item/item.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace {

class Clonable_item : public erhe::Item<erhe::Item_base, erhe::Item_base, Clonable_item>
{
public:
    using Item::Item;
    static constexpr std::string_view static_type_name{"Clonable_item"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::mesh; }
};

class Not_clonable_item : public erhe::Item<erhe::Item_base, erhe::Item_base, Not_clonable_item, erhe::Item_kind::not_clonable>
{
public:
    using Item::Item;
    static constexpr std::string_view static_type_name{"Not_clonable_item"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::camera; }
};

class Custom_clone_item : public erhe::Item<erhe::Item_base, erhe::Item_base, Custom_clone_item, erhe::Item_kind::clone_using_custom_clone_constructor>
{
public:
    using Item::Item;
    static constexpr std::string_view static_type_name{"Custom_clone_item"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::light; }

    Custom_clone_item(const Custom_clone_item& other, erhe::for_clone)
        : Item{other}
        , custom_clone_called{true}
    {
    }

    bool custom_clone_called{false};
};

TEST(ItemCRTP, GetType)
{
    auto item = std::make_shared<Clonable_item>("test");
    EXPECT_EQ(item->get_type(), Clonable_item::get_static_type());
    EXPECT_EQ(item->get_type(), erhe::Item_type::mesh);
}

TEST(ItemCRTP, GetTypeName)
{
    auto item = std::make_shared<Clonable_item>("test");
    EXPECT_EQ(item->get_type_name(), "Clonable_item");
}

TEST(ItemCRTP, CloneCopyConstructor)
{
    auto item = std::make_shared<Clonable_item>("original");
    item->show();

    auto cloned = item->clone();
    ASSERT_NE(cloned, nullptr);
    EXPECT_NE(cloned->get_id(), item->get_id());
    EXPECT_EQ(cloned->get_name(), "original Copy");
    EXPECT_TRUE(cloned->is_visible());
}

TEST(ItemCRTP, CloneNotClonable)
{
    auto item = std::make_shared<Not_clonable_item>("test");
    auto cloned = item->clone();
    EXPECT_EQ(cloned, nullptr);
}

TEST(ItemCRTP, CloneCustomConstructor)
{
    auto item = std::make_shared<Custom_clone_item>("test");
    auto cloned_base = item->clone();
    ASSERT_NE(cloned_base, nullptr);

    auto cloned = std::dynamic_pointer_cast<Custom_clone_item>(cloned_base);
    ASSERT_NE(cloned, nullptr);
    EXPECT_TRUE(cloned->custom_clone_called);
}

TEST(ItemCRTP, IsTemplateFunction)
{
    auto mesh_item   = std::make_shared<Clonable_item>("mesh");
    auto camera_item = std::make_shared<Not_clonable_item>("camera");

    EXPECT_TRUE(erhe::is<Clonable_item>(mesh_item.get()));
    EXPECT_FALSE(erhe::is<Not_clonable_item>(mesh_item.get()));

    EXPECT_TRUE(erhe::is<Not_clonable_item>(camera_item.get()));
    EXPECT_FALSE(erhe::is<Clonable_item>(camera_item.get()));
}

TEST(ItemCRTP, IsWithNullptr)
{
    EXPECT_FALSE(erhe::is<Clonable_item>(static_cast<erhe::Item_base*>(nullptr)));
}

TEST(ItemCRTP, IsWithSharedPtr)
{
    std::shared_ptr<erhe::Item_base> item = std::make_shared<Clonable_item>("test");
    EXPECT_TRUE(erhe::is<Clonable_item>(item));
    EXPECT_FALSE(erhe::is<Not_clonable_item>(item));
}

} // namespace
