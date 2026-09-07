// Inherited flags (D23 in doc/property-system.md): visible,
// shadow_cast and lightmapped are inherits-flagged properties whose
// effective value is mirrored into Item_flags::derived.

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

using namespace erhe::property;

class Leaf : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Leaf>
{
public:
    explicit Leaf(const std::string_view name) : Item{name} {}
    explicit Leaf(const Leaf& other) = default;
    static constexpr std::string_view static_type_name{"Leaf"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return uint64_t{1} << 57; }

    std::vector<uint64_t> flag_updates;

    void handle_flag_bits_update(const uint64_t old_flag_bits, const uint64_t new_flag_bits) override
    {
        flag_updates.push_back(old_flag_bits ^ new_flag_bits);
    }
};

} // namespace

TEST(Item_visibility, defaults_are_the_property_defaults)
{
    auto item = std::make_shared<Leaf>("i");
    EXPECT_TRUE (item->is_visible());
    EXPECT_TRUE (item->get_value(erhe::Item_base::visible_property));
    EXPECT_EQ(item->get_flag_bits() & erhe::Item_flags::derived, erhe::Item_flags::visible | erhe::Item_flags::active);
    EXPECT_EQ(item->get_value_source(erhe::Item_base::visible_property.get()), Value_source::default_value);
}

TEST(Item_visibility, derived_bit_follows_local_value)
{
    auto item = std::make_shared<Leaf>("i");
    item->hide();
    EXPECT_FALSE(item->is_visible());
    EXPECT_EQ(item->read_local_value(erhe::Item_base::visible_property), std::optional<bool>{false});
    ASSERT_EQ(item->flag_updates.size(), std::size_t{1});
    EXPECT_EQ(item->flag_updates[0], erhe::Item_flags::visible);

    item->show();
    EXPECT_TRUE(item->is_visible());
    item->set_visible(false);
    EXPECT_FALSE(item->is_visible());
}

TEST(Item_visibility, derived_bit_follows_inherited_value)
{
    auto root = std::make_shared<Leaf>("root");
    auto mid  = std::make_shared<Leaf>("mid");
    auto leaf = std::make_shared<Leaf>("leaf");
    mid->set_parent(root);
    leaf->set_parent(mid);

    root->hide();
    EXPECT_FALSE(mid->is_visible());
    EXPECT_FALSE(leaf->is_visible());
    EXPECT_EQ(leaf->get_value_source(erhe::Item_base::visible_property.get()), Value_source::inherited);
    ASSERT_EQ(leaf->flag_updates.size(), std::size_t{1});

    // A local true under a hidden ancestor shows the item (closest local wins).
    mid->show();
    EXPECT_TRUE(mid->is_visible());
    EXPECT_TRUE(leaf->is_visible());

    mid->clear_value(erhe::Item_base::visible_property);
    EXPECT_FALSE(mid->is_visible());
    EXPECT_FALSE(leaf->is_visible());

    root->show();
    EXPECT_TRUE(leaf->is_visible());
}

TEST(Item_visibility, derived_bit_follows_tree_change)
{
    auto hidden  = std::make_shared<Leaf>("hidden");
    auto shown   = std::make_shared<Leaf>("shown");
    auto child   = std::make_shared<Leaf>("child");
    hidden->hide();

    child->set_parent(hidden);
    EXPECT_FALSE(child->is_visible());
    ASSERT_EQ(child->flag_updates.size(), std::size_t{1});

    child->set_parent(shown);
    EXPECT_TRUE(child->is_visible());
    ASSERT_EQ(child->flag_updates.size(), std::size_t{2});

    child->set_parent(shown); // no change, no notification
    ASSERT_EQ(child->flag_updates.size(), std::size_t{2});
}

TEST(Item_visibility, set_flag_bits_drops_derived_bits)
{
    auto item = std::make_shared<Leaf>("i");
    item->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::shadow_cast);
    EXPECT_NE(item->get_flag_bits() & erhe::Item_flags::content, 0u);
    EXPECT_EQ(item->get_flag_bits() & erhe::Item_flags::shadow_cast, 0u);

    item->disable_flag_bits(erhe::Item_flags::visible);
    EXPECT_TRUE(item->is_visible());
}

TEST(Item_visibility, copy_rederives_bits)
{
    auto parent = std::make_shared<Leaf>("parent");
    auto child  = std::make_shared<Leaf>("child");
    child->set_parent(parent);
    parent->hide();
    EXPECT_FALSE(child->is_visible());

    // The copy has no parent: the inherited false does not survive.
    auto copy = std::make_shared<Leaf>(*child);
    EXPECT_TRUE (copy->is_visible());
    EXPECT_FALSE(copy->has_local_value(erhe::Item_base::visible_property.get()));

    auto hidden_copy = std::make_shared<Leaf>(*parent);
    EXPECT_FALSE(hidden_copy->is_visible());
}
