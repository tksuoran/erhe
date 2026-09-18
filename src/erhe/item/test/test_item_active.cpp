// The USD `active` state (doc/erhe/usd_compatibility_design.md X2): `active` is
// the item's own opinion (not an inherits-flagged property), and the
// derived Item_flags::active bit carries USD's subtree pruning - an item
// below an inactive one is inactive whatever it says of itself.

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
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return uint64_t{1} << 58; }

    std::vector<uint64_t> flag_updates;

    void handle_flag_bits_update(const uint64_t old_flag_bits, const uint64_t new_flag_bits) override
    {
        flag_updates.push_back(old_flag_bits ^ new_flag_bits);
    }
};

void set_active(const std::shared_ptr<Leaf>& item, const bool value)
{
    item->set_value(erhe::Item_base::active_property, value);
}

} // namespace

TEST(Item_active, default_is_active)
{
    auto item = std::make_shared<Leaf>("i");
    EXPECT_TRUE(item->is_active());
    EXPECT_TRUE(item->get_value(erhe::Item_base::active_property));
    EXPECT_NE(item->get_flag_bits() & erhe::Item_flags::active, 0u);
    EXPECT_EQ(item->get_value_source(erhe::Item_base::active_property.get()), Value_source::default_value);
}

TEST(Item_active, derived_bit_follows_local_value)
{
    auto item = std::make_shared<Leaf>("i");
    set_active(item, false);
    EXPECT_FALSE(item->is_active());
    EXPECT_EQ(item->read_local_value(erhe::Item_base::active_property), std::optional<bool>{false});
    ASSERT_EQ(item->flag_updates.size(), std::size_t{1});
    EXPECT_EQ(item->flag_updates[0], erhe::Item_flags::active);

    // One notification per change: writing the same value again changes
    // nothing.
    set_active(item, false);
    ASSERT_EQ(item->flag_updates.size(), std::size_t{1});

    set_active(item, true);
    EXPECT_TRUE(item->is_active());
    ASSERT_EQ(item->flag_updates.size(), std::size_t{2});
}

TEST(Item_active, parent_value_clears_the_whole_subtree)
{
    auto root = std::make_shared<Leaf>("root");
    auto mid  = std::make_shared<Leaf>("mid");
    auto leaf = std::make_shared<Leaf>("leaf");
    mid->set_parent(root);
    leaf->set_parent(mid);

    // A local true on a descendant does not survive an inactive ancestor:
    // USD prunes the subtree.
    set_active(mid, true);
    set_active(root, false);
    EXPECT_FALSE(root->is_active());
    EXPECT_FALSE(mid ->is_active());
    EXPECT_FALSE(leaf->is_active());
    ASSERT_EQ(leaf->flag_updates.size(), std::size_t{1});
    EXPECT_EQ(leaf->flag_updates[0], erhe::Item_flags::active);

    // The descendant's own value is untouched: the bit is derived.
    EXPECT_TRUE(mid->get_value(erhe::Item_base::active_property));
    EXPECT_EQ(mid->get_value_source(erhe::Item_base::active_property.get()), Value_source::local);

    set_active(root, true);
    EXPECT_TRUE(root->is_active());
    EXPECT_TRUE(mid ->is_active());
    EXPECT_TRUE(leaf->is_active());
}

TEST(Item_active, an_inactive_item_of_the_subtree_stays_inactive)
{
    auto root = std::make_shared<Leaf>("root");
    auto mid  = std::make_shared<Leaf>("mid");
    auto leaf = std::make_shared<Leaf>("leaf");
    mid->set_parent(root);
    leaf->set_parent(mid);

    set_active(mid,  false);
    set_active(root, false);
    EXPECT_FALSE(mid ->is_active());
    EXPECT_FALSE(leaf->is_active());

    // Reactivating the root restores only what is itself active.
    set_active(root, true);
    EXPECT_TRUE (root->is_active());
    EXPECT_FALSE(mid ->is_active());
    EXPECT_FALSE(leaf->is_active());

    set_active(mid, true);
    EXPECT_TRUE(mid ->is_active());
    EXPECT_TRUE(leaf->is_active());
}

TEST(Item_active, derived_bit_follows_tree_change)
{
    auto inactive = std::make_shared<Leaf>("inactive");
    auto active   = std::make_shared<Leaf>("active");
    auto child    = std::make_shared<Leaf>("child");
    auto grand    = std::make_shared<Leaf>("grand");
    grand->set_parent(child);
    set_active(inactive, false);

    child->set_parent(inactive);
    EXPECT_FALSE(child->is_active());
    EXPECT_FALSE(grand->is_active());
    ASSERT_EQ(child->flag_updates.size(), std::size_t{1});

    child->set_parent(active);
    EXPECT_TRUE(child->is_active());
    EXPECT_TRUE(grand->is_active());
    ASSERT_EQ(child->flag_updates.size(), std::size_t{2});

    child->set_parent(active); // no change, no notification
    ASSERT_EQ(child->flag_updates.size(), std::size_t{2});
}

TEST(Item_active, set_flag_bits_drops_the_derived_bit)
{
    auto item = std::make_shared<Leaf>("i");
    item->disable_flag_bits(erhe::Item_flags::active);
    EXPECT_TRUE(item->is_active());

    set_active(item, false);
    item->enable_flag_bits(erhe::Item_flags::active);
    EXPECT_FALSE(item->is_active());
}

TEST(Item_active, copy_rederives_the_bit)
{
    auto parent = std::make_shared<Leaf>("parent");
    auto child  = std::make_shared<Leaf>("child");
    child->set_parent(parent);
    set_active(parent, false);
    EXPECT_FALSE(child->is_active());

    // The copy has no parent, so it uses its own value.
    Leaf copy{*child.get()};
    EXPECT_TRUE(copy.is_active());

    set_active(child, false);
    Leaf inactive_copy{*child.get()};
    EXPECT_FALSE(inactive_copy.is_active());
}

// Draw-mode pruning (doc/erhe/usd_compatibility.md, "Draw modes"): a prim whose
// draw mode asks for a proxy keeps its own bit and takes its children's
// subtrees out, the way an inactive prim's subtree goes out.

TEST(Item_active, pruning_parent_clears_the_children_subtrees)
{
    auto root = std::make_shared<Leaf>("root");
    auto mid  = std::make_shared<Leaf>("mid");
    auto leaf = std::make_shared<Leaf>("leaf");
    mid->set_parent(root);
    leaf->set_parent(mid);

    EXPECT_FALSE(root->prunes_children());

    root->set_prunes_children(true);
    EXPECT_TRUE (root->prunes_children());
    // The pruning prim itself stays active: it carries the proxy.
    EXPECT_TRUE (root->is_active());
    EXPECT_FALSE(mid ->is_active());
    EXPECT_FALSE(leaf->is_active());
    ASSERT_EQ(mid->flag_updates.size(), std::size_t{1});
    EXPECT_EQ(mid->flag_updates[0], erhe::Item_flags::active);

    // The children's own opinions are untouched - the pruning is derived
    // state of the parent, never written into `active`.
    EXPECT_TRUE(mid->get_value(erhe::Item_base::active_property));
    EXPECT_EQ(mid->get_value_source(erhe::Item_base::active_property.get()), Value_source::default_value);

    root->set_prunes_children(false);
    EXPECT_TRUE(mid ->is_active());
    EXPECT_TRUE(leaf->is_active());

    root->set_prunes_children(false); // no change, no notification
    ASSERT_EQ(mid->flag_updates.size(), std::size_t{2});
}

TEST(Item_active, pruning_and_the_items_own_opinion_are_both_required)
{
    auto root = std::make_shared<Leaf>("root");
    auto mid  = std::make_shared<Leaf>("mid");
    mid->set_parent(root);

    set_active(mid, false);
    root->set_prunes_children(true);
    EXPECT_FALSE(mid->is_active());

    // Lifting the pruning leaves the item's own `active = false` standing.
    root->set_prunes_children(false);
    EXPECT_FALSE(mid->is_active());
    set_active(mid, true);
    EXPECT_TRUE(mid->is_active());
}

TEST(Item_active, pruning_follows_the_tree_change)
{
    auto pruning = std::make_shared<Leaf>("pruning");
    auto plain   = std::make_shared<Leaf>("plain");
    auto child   = std::make_shared<Leaf>("child");
    auto grand   = std::make_shared<Leaf>("grand");
    grand->set_parent(child);
    pruning->set_prunes_children(true);

    child->set_parent(pruning);
    EXPECT_FALSE(child->is_active());
    EXPECT_FALSE(grand->is_active());

    child->set_parent(plain);
    EXPECT_TRUE(child->is_active());
    EXPECT_TRUE(grand->is_active());
}
