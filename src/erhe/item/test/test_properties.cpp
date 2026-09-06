// Item_base as an erhe::property::Dependency_object: metadata resolved by
// item type, inheritance through Hierarchy, clone semantics.

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace erhe::property;

// Test item types with distinct Item_type bits (far above the real ones).
constexpr uint64_t c_type_widget = uint64_t{1} << 55;
constexpr uint64_t c_type_gadget = uint64_t{1} << 56;

class Widget : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Widget>
{
public:
    explicit Widget(const std::string_view name) : Item{name} {}
    explicit Widget(const Widget& other) = default;
    static constexpr std::string_view static_type_name{"Widget"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return c_type_widget; }

    std::vector<std::string> changed_names;

protected:
    void on_property_changed(const Property_changed_args& args) override
    {
        changed_names.emplace_back(args.property.get_name());
    }
};

class Gadget : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Gadget>
{
public:
    explicit Gadget(const std::string_view name) : Item{name} {}
    explicit Gadget(const Gadget& other) = default;
    static constexpr std::string_view static_type_name{"Gadget"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return c_type_gadget; }
};

const Property<float> widget_tint  = Property<float>::register_property("tint", Widget::property_owner_type(), Property_metadata{.default_value = 1.0f, .inherits = true});
const Property<int>   widget_count = Property<int>::register_property("count", Widget::property_owner_type(), Property_metadata{.default_value = 2});

// The same property name registered for another type is a distinct property.
const Property<float> gadget_tint = Property<float>::register_property("tint", Gadget::property_owner_type(), Property_metadata{.default_value = 9.0f});

// A style-like item: its secondary owner type is the root type, so it holds
// every class's value properties and applies to every item, the editor's
// Style item's rule (doc/style-library.md D2).
constexpr uint64_t c_type_look = uint64_t{1} << 57;
class Look : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Look>
{
public:
    explicit Look(const std::string_view name) : Item{name} {}
    explicit Look(const Look& other) = default;
    static constexpr std::string_view static_type_name{"Look"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return c_type_look; }
    [[nodiscard]] auto get_secondary_property_owner_type() const -> std::optional<Owner_type> override { return root_owner_type; }
};

} // anonymous namespace

TEST(Item_properties, item_type_drives_metadata_and_lookup)
{
    auto widget = std::make_shared<Widget>("w");
    auto gadget = std::make_shared<Gadget>("g");
    EXPECT_EQ(widget->get_property_owner_type(), Widget::property_owner_type());
    EXPECT_EQ(get_owner_type_parent(Widget::property_owner_type()), erhe::Hierarchy::property_owner_type());
    EXPECT_EQ(get_owner_type_parent(erhe::Hierarchy::property_owner_type()), erhe::Item_base::property_owner_type());
    EXPECT_EQ(widget->get_value(widget_tint), 1.0f);
    EXPECT_EQ(gadget->get_value(gadget_tint), 9.0f);
    EXPECT_EQ(Property_registry::get().find(Widget::property_owner_type(), "tint"), widget_tint.get_ptr());
    EXPECT_EQ(Property_registry::get().find(Gadget::property_owner_type(), "tint"), gadget_tint.get_ptr());
    // A base item property is found for a derived item through the chain.
    EXPECT_EQ(Property_registry::get().find_for_object(Widget::property_owner_type(), "visible"), erhe::Item_base::visible_property.get_ptr());
    EXPECT_EQ(Property_registry::get().find_for_object(Widget::property_owner_type(), "child_count"), erhe::Hierarchy::child_count_property.get_ptr());

    std::vector<std::string> names;
    // Item_base's and Hierarchy's properties (visible, shadow_cast,
    // lightmapped, child_count) are listed before Widget's own; keep only
    // Widget's.
    Property_registry::get().for_each_property_of_object(
        widget->get_property_owner_type(),
        [&](const Dependency_property& p) {
            if (p.get_owner_type() == Widget::property_owner_type()) {
                names.emplace_back(p.get_name());
            }
        }
    );
    ASSERT_EQ(names.size(), std::size_t{2});
    EXPECT_EQ(names[0], "tint");
    EXPECT_EQ(names[1], "count");
}

TEST(Item_properties, inherits_through_hierarchy_three_levels)
{
    auto root = std::make_shared<Widget>("root");
    auto mid  = std::make_shared<Widget>("mid");
    auto leaf = std::make_shared<Widget>("leaf");
    mid->set_parent(root);
    leaf->set_parent(mid);

    root->set_value(widget_tint, 0.5f);
    EXPECT_EQ(mid->get_value(widget_tint), 0.5f);
    EXPECT_EQ(leaf->get_value(widget_tint), 0.5f);
    EXPECT_EQ(leaf->get_value_source(widget_tint.get()), Value_source::inherited);
    EXPECT_EQ(leaf->changed_names.size(), std::size_t{1});

    // A local value in the middle shadows the subtree below it.
    mid->set_value(widget_tint, 0.25f);
    root->set_value(widget_tint, 0.75f);
    EXPECT_EQ(leaf->get_value(widget_tint), 0.25f);
    EXPECT_EQ(leaf->changed_names.size(), std::size_t{2}); // mid's change, not root's second one

    root->clear_value(widget_tint);
    EXPECT_EQ(mid->get_value(widget_tint), 0.25f);
    mid->clear_value(widget_tint);
    EXPECT_EQ(leaf->get_value(widget_tint), 1.0f);
    EXPECT_EQ(leaf->get_value_source(widget_tint.get()), Value_source::default_value);
}

TEST(Item_properties, reparent_re_reads_inherited_values)
{
    auto a     = std::make_shared<Widget>("a");
    auto b     = std::make_shared<Widget>("b");
    auto child = std::make_shared<Widget>("child");
    auto grand = std::make_shared<Widget>("grand");
    a->set_value(widget_tint, 0.1f);
    b->set_value(widget_tint, 0.2f);
    child->set_parent(a);
    grand->set_parent(child);
    child->changed_names.clear();
    grand->changed_names.clear();

    child->set_parent(b);
    EXPECT_EQ(child->get_value(widget_tint), 0.2f);
    EXPECT_EQ(grand->get_value(widget_tint), 0.2f);
    EXPECT_EQ(child->changed_names.size(), std::size_t{1});
    EXPECT_EQ(grand->changed_names.size(), std::size_t{1});

    // Repositioning within the same parent is not a parent change.
    auto sibling = std::make_shared<Widget>("sibling");
    sibling->set_parent(b);
    child->changed_names.clear();
    child->set_parent(b, 0);
    EXPECT_TRUE(child->changed_names.empty());

    // remove() splices the node out and reparents its children to the
    // grandparent: grand now inherits from b directly.
    grand->changed_names.clear();
    child->remove();
    EXPECT_EQ(grand->get_parent().lock().get(), b.get());
    EXPECT_EQ(grand->get_value(widget_tint), 0.2f);
    EXPECT_TRUE(grand->changed_names.empty());

    child->set_parent(nullptr);
    grand->set_parent(nullptr);
    EXPECT_EQ(grand->get_value(widget_tint), 1.0f);
    EXPECT_EQ(grand->changed_names.size(), std::size_t{1});
}

TEST(Item_properties, clone_keeps_local_values_only)
{
    auto root = std::make_shared<Widget>("root");
    auto leaf = std::make_shared<Widget>("leaf");
    leaf->set_parent(root);
    root->set_value(widget_tint, 0.5f);
    leaf->set_value(widget_count, 7);

    std::shared_ptr<erhe::Item_base> clone_base = leaf->clone();
    auto clone = std::dynamic_pointer_cast<Widget>(clone_base);
    ASSERT_TRUE(clone);
    EXPECT_EQ(clone->get_value(widget_count), 7);
    EXPECT_EQ(clone->read_local_value(widget_count).value(), 7);
    EXPECT_FALSE(clone->has_local_value(widget_tint.get()));
    EXPECT_EQ(clone->get_value(widget_tint), 1.0f); // orphan clone: default, not root's value

    clone->set_parent(root);
    EXPECT_EQ(clone->get_value(widget_tint), 0.5f);
}

namespace {

// A non-hierarchy item whose inheritance parent is the container that
// wraps it (Item_base::set_inheritance_container), the way an editor
// content-library node wraps a material.
class Leaf : public erhe::Item<erhe::Item_base, erhe::Item_base, Leaf>
{
public:
    explicit Leaf(const std::string_view name) : Item{name} {}
    explicit Leaf(const Leaf& other) = default;
    static constexpr std::string_view static_type_name{"Leaf"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return uint64_t{1} << 57; }

    std::vector<std::string> changed_names;

protected:
    void on_property_changed(const Property_changed_args& args) override
    {
        changed_names.emplace_back(args.property.get_name());
    }
};

class Wrapper : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Wrapper>
{
public:
    explicit Wrapper(const std::string_view name) : Item{name} {}
    explicit Wrapper(const Wrapper& other) = default;
    static constexpr std::string_view static_type_name{"Wrapper"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return uint64_t{1} << 58; }

    std::shared_ptr<Leaf> item;

    void handle_add_child(const std::shared_ptr<erhe::Hierarchy>& child_node, const std::size_t position) override
    {
        Hierarchy::handle_add_child(child_node, position);
        Wrapper* const child = dynamic_cast<Wrapper*>(child_node.get());
        if ((child != nullptr) && child->item) {
            child->item->set_inheritance_container(child);
        }
    }

    void for_each_inheritance_child(const std::function<void(Dependency_object&)>& callback) override
    {
        Hierarchy::for_each_inheritance_child(callback);
        if (item) {
            callback(*item);
        }
    }
};

} // anonymous namespace

TEST(Item_properties, container_link_inherits_and_notifies)
{
    auto folder = std::make_shared<Wrapper>("folder");
    auto entry  = std::make_shared<Wrapper>("entry");
    auto leaf   = std::make_shared<Leaf>("leaf");
    entry->item = leaf;

    // Outside a container the leaf has no inheritance parent.
    EXPECT_EQ(leaf->get_inheritance_parent(), nullptr);
    EXPECT_EQ(leaf->get_inheritance_container(), nullptr);

    // Attaching under a folder that already holds a local value notifies
    // the leaf of the value it now inherits (the set_parent snapshot).
    folder->set_value(erhe::Item_base::visible_property, false);
    entry->set_parent(folder);
    EXPECT_EQ(leaf->get_inheritance_parent(), entry.get());
    EXPECT_FALSE(leaf->get_value(erhe::Item_base::visible_property));
    EXPECT_EQ(leaf->get_value_source(erhe::Item_base::visible_property.get()), Value_source::inherited);
    EXPECT_FALSE(leaf->is_visible());
    ASSERT_EQ(leaf->changed_names.size(), std::size_t{1});
    EXPECT_EQ(leaf->changed_names[0], "visible");

    // A folder set / clear reaches the leaf through the entry node.
    folder->clear_value(erhe::Item_base::visible_property);
    EXPECT_TRUE(leaf->get_value(erhe::Item_base::visible_property));
    EXPECT_EQ(leaf->changed_names.size(), std::size_t{2});

    // The clone of the leaf starts outside any container.
    auto clone = std::dynamic_pointer_cast<Leaf>(leaf->clone());
    ASSERT_TRUE(clone);
    EXPECT_EQ(clone->get_inheritance_container(), nullptr);
}

// Item-level bridged properties (D18): name, tags and the persistent flag
// bits read and write the members.
TEST(Item_properties, name_and_flag_bridges)
{
    auto widget = std::make_shared<Widget>("w");
    EXPECT_EQ(std::get<std::string>(widget->get_value(erhe::Item_base::name_property.get())), "w");
    EXPECT_EQ(widget->get_value_source(erhe::Item_base::name_property.get()), Value_source::local);
    EXPECT_TRUE(widget->set_value(erhe::Item_base::name_property.get(), Property_value{std::string{"widget"}}));
    EXPECT_EQ(widget->get_name(), "widget");

    EXPECT_FALSE(widget->get_value(erhe::Item_base::lock_viewport_selection_property));
    EXPECT_TRUE(widget->set_value(erhe::Item_base::lock_viewport_selection_property.get(), Property_value{true}));
    EXPECT_TRUE(widget->is_lock_viewport_selection());
    widget->set_flag_bits(erhe::Item_flags::lock_viewport_selection, false);
    EXPECT_FALSE(widget->get_value(erhe::Item_base::lock_viewport_selection_property));
    widget->enable_flag_bits(erhe::Item_flags::show_in_ui);
    EXPECT_TRUE(widget->get_value(erhe::Item_base::show_in_ui_property));
}

TEST(Item_properties, tags_bridge)
{
    auto widget = std::make_shared<Widget>("w");
    EXPECT_EQ(std::get<std::string>(widget->get_value(erhe::Item_base::tags_property.get())), "");
    widget->add_tag("b");
    widget->add_tag("a");
    EXPECT_EQ(std::get<std::string>(widget->get_value(erhe::Item_base::tags_property.get())), "a, b");

    EXPECT_TRUE(widget->set_value(erhe::Item_base::tags_property.get(), Property_value{std::string{" red ,blue,, , green\t"}}));
    EXPECT_EQ(widget->get_tags(), (std::set<std::string>{"red", "blue", "green"}));
    EXPECT_EQ(std::get<std::string>(widget->get_value(erhe::Item_base::tags_property.get())), "blue, green, red");

    EXPECT_TRUE(widget->clear_value(erhe::Item_base::tags_property.get()));
    EXPECT_TRUE(widget->get_tags().empty());
}

TEST(Item_properties, style_property_chain_and_cycle)
{
    // The style row (doc/style-library.md D3) over a chain of styles: the
    // bridge assigns through set_style, so an assignment whose chain reaches
    // the item is refused and the item keeps its style (D25 style chain).
    auto a = std::make_shared<Look>("a");
    auto b = std::make_shared<Look>("b");
    auto widget = std::make_shared<Widget>("w");
    const Dependency_property& style = erhe::Item_base::style_property.get();

    a->set_value(widget_tint, 4.0f);
    EXPECT_TRUE(b->set_value(style, Property_value{Object_reference{a}}));
    EXPECT_TRUE(widget->set_value(style, Property_value{Object_reference{b}}));
    EXPECT_EQ(widget->get_value(widget_tint), 4.0f);
    EXPECT_EQ(widget->get_value_source(widget_tint.get()), Value_source::style);

    // a -> b -> a, and a -> a: refused, nothing changes.
    a->set_value(style, Property_value{Object_reference{b}});
    EXPECT_EQ(a->get_style(), nullptr);
    a->set_value(style, Property_value{Object_reference{a}});
    EXPECT_EQ(a->get_style(), nullptr);
    EXPECT_EQ(b->get_style(), a);
    EXPECT_EQ(widget->get_value(widget_tint), 4.0f);
}
