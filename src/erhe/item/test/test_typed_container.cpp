// A prim with no parent of its own (doc/usd-compatibility-plan.md U4: a
// content-library resource that has the Typed base but no place in the tree
// yet) inherits from, and shares the namespace of, the container that holds
// it - the rule Item_base states and Hierarchy keeps when it has no parent.

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_property/dependency_property.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using namespace erhe::property;

// Stands in for Content_library_node: a hierarchy node that wraps one item,
// is that item's inheritance container, and pushes its values to it.
class Entry_node : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Entry_node>
{
public:
    explicit Entry_node(const std::string_view name) : Item{name} {}
    explicit Entry_node(const Entry_node& other) = default;
    static constexpr std::string_view static_type_name{"Entry_node"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return uint64_t{1} << 57; }

    void set_item(const std::shared_ptr<erhe::Item_base>& item)
    {
        m_item = item;
        m_item->set_inheritance_container(this);
    }

    void for_each_inheritance_child(const std::function<void(Dependency_object&)>& callback) override
    {
        erhe::Hierarchy::for_each_inheritance_child(callback);
        if (m_item) {
            callback(*m_item);
        }
    }

private:
    std::shared_ptr<erhe::Item_base> m_item;
};

// A typed prim that carries one inheritable value, as a library resource does.
class Resource_prim : public erhe::Item<erhe::Item_base, erhe::Typed, Resource_prim>
{
public:
    explicit Resource_prim(const std::string_view name) : Item{name} {}
    explicit Resource_prim(const Resource_prim& other) = default;
    static constexpr std::string_view static_type_name{"Resource_prim"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::Typed::get_static_type() | (uint64_t{1} << 56);
    }
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Resource_prim"; }

    static const Property<int> hue_property;
};

const Property<int> Resource_prim::hue_property = Property<int>::register_property(
    "hue", Resource_prim::property_owner_type(),
    Property_metadata{.default_value = make_value(1), .inherits = true}
);

} // namespace

TEST(Typed_container, prim_inherits_from_its_container)
{
    const std::shared_ptr<Entry_node>    entry = std::make_shared<Entry_node>("Copper");
    const std::shared_ptr<Resource_prim> prim  = std::make_shared<Resource_prim>("Copper");
    entry->set_item(prim);

    EXPECT_TRUE(erhe::is<erhe::Typed>(prim));
    EXPECT_EQ(prim->get_prim_type_name(), "Resource_prim");
    // No parent of its own, so the container is the inheritance parent.
    EXPECT_EQ(prim->get_parent().lock(), nullptr);
    EXPECT_EQ(prim->get_inheritance_parent(), entry.get());

    EXPECT_EQ(prim->get_value(Resource_prim::hue_property), 1);
    EXPECT_TRUE(entry->set_value(Resource_prim::hue_property.get(), make_value(9)));
    EXPECT_EQ(prim->get_value(Resource_prim::hue_property), 9);
    EXPECT_EQ(prim->get_value_source(Resource_prim::hue_property.get()), Value_source::inherited);
}

TEST(Typed_container, prim_shares_the_namespace_of_its_container)
{
    const std::shared_ptr<Entry_node> folder = std::make_shared<Entry_node>("Materials");
    const std::shared_ptr<Entry_node> first  = std::make_shared<Entry_node>("Copper");
    const std::shared_ptr<Entry_node> second = std::make_shared<Entry_node>("Iron");
    first ->set_parent(folder);
    second->set_parent(folder);

    const std::shared_ptr<Resource_prim> prim = std::make_shared<Resource_prim>("Copper");
    first->set_item(prim);

    EXPECT_TRUE (prim->is_name_available("Bronze"));
    EXPECT_TRUE (prim->is_name_available("Copper")); // its own entry's name
    EXPECT_FALSE(prim->is_name_available("Iron"));   // held by a sibling entry
}
