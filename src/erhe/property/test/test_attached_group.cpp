// An attached value group keyed on one of its values (doc/erhe/property_system.md
// section 4.23): the holder carries the group while the key property's
// effective value differs from the holder's own default, the rest of the
// group is listed on exactly those holders, and Add Property offers the key
// everywhere else.

#include "erhe_property/attached_group.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_metadata.hpp"

#include "test_object.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

using erhe::property::Dependency_object;
using erhe::property::Dependency_property;
using erhe::property::Owner_type;
using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_registry;
using erhe::property::Property_ui;
using erhe::property::Value_source;
using erhe::property::attached_group_visible_when;
using erhe::property::carries_attached_group;
using erhe::property::is_attached_property_listed;
using erhe::property::test::Test_object;
using erhe::property::test::type_a;
using erhe::property::test::type_a_child;
using erhe::property::test::type_b;

// The registering class of the group. Its values are held by type_a objects
// and their descendants, the way editor::Ik registers on erhe::scene::Node.
[[nodiscard]] auto group_owner_type() -> Owner_type
{
    static const Owner_type id = erhe::property::allocate_owner_type(erhe::property::root_owner_type, "Test_group");
    return id;
}

// The key property, registered first so the rest of the group can take its
// visible_when. It does not inherit: the feature belongs to the holder that
// carries it, never to the holders below it.
const Property<bool> key_property = Property<bool>::register_attached(
    "enabled", group_owner_type(), type_a(),
    Property_metadata{.default_value = false, .inherits = false, .ui = Property_ui{.group = "Test group", .label = "Enabled"}}
);
const Property<float> strength_property = Property<float>::register_attached(
    "strength", group_owner_type(), type_a(),
    Property_metadata{
        .default_value = 1.0f,
        .ui            = Property_ui{.group = "Test group", .label = "Strength", .visible_when = attached_group_visible_when(key_property.get())}
    }
);

// The addable candidates of an object: every attached property of its type
// the listing rule does not list, which is what the editor's Add Property row
// offers (D12).
[[nodiscard]] auto collect_addable(const Dependency_object& object) -> std::vector<const Dependency_property*>
{
    std::vector<const Dependency_property*> candidates;
    Property_registry::get().for_each_attached_property_of(
        object.get_property_owner_type(),
        [&object, &candidates](const Dependency_property& property) {
            if (!is_attached_property_listed(object, property)) {
                candidates.push_back(&property);
            }
        }
    );
    return candidates;
}

[[nodiscard]] auto contains(const std::vector<const Dependency_property*>& candidates, const Dependency_property& property) -> bool
{
    return std::find(candidates.begin(), candidates.end(), &property) != candidates.end();
}

} // anonymous namespace

TEST(Attached_group, key_at_its_default_means_the_feature_is_absent)
{
    Test_object holder{type_a()};

    EXPECT_FALSE(carries_attached_group(holder, key_property.get()));
    EXPECT_FALSE(is_attached_property_listed(holder, strength_property.get()));
    EXPECT_FALSE(is_attached_property_listed(holder, key_property.get()));

    // Add Property offers the whole group, the key included: that is how the
    // feature is added to a holder that does not carry it.
    const std::vector<const Dependency_property*> candidates = collect_addable(holder);
    EXPECT_TRUE(contains(candidates, key_property.get()));
    EXPECT_TRUE(contains(candidates, strength_property.get()));
}

TEST(Attached_group, a_local_key_value_lists_the_group)
{
    Test_object holder{type_a()};
    holder.set_value(key_property, true);

    EXPECT_TRUE(carries_attached_group(holder, key_property.get()));
    EXPECT_TRUE(is_attached_property_listed(holder, strength_property.get()));
    EXPECT_TRUE(is_attached_property_listed(holder, key_property.get())); // its own local value lists it

    const std::vector<const Dependency_property*> candidates = collect_addable(holder);
    EXPECT_FALSE(contains(candidates, key_property.get()));
    EXPECT_FALSE(contains(candidates, strength_property.get()));
}

TEST(Attached_group, clearing_the_key_takes_the_group_away_again)
{
    Test_object holder{type_a()};
    holder.set_value(key_property, true);
    holder.set_value(strength_property, 0.25f);

    holder.clear_value(key_property);
    EXPECT_FALSE(carries_attached_group(holder, key_property.get()));
    EXPECT_EQ(holder.get_value_source(key_property.get()), Value_source::default_value);

    // The strength keeps its local value, so its row stays listed and
    // resettable; the group is gone as far as the key says.
    EXPECT_TRUE(is_attached_property_listed(holder, strength_property.get()));
    holder.clear_value(strength_property);
    EXPECT_FALSE(is_attached_property_listed(holder, strength_property.get()));
}

TEST(Attached_group, the_key_does_not_reach_the_holders_below)
{
    std::shared_ptr<Test_object> parent = std::make_shared<Test_object>(type_a());
    std::shared_ptr<Test_object> child  = std::make_shared<Test_object>(type_a_child());
    child->set_parent(parent.get());

    parent->set_value(key_property, true);
    EXPECT_TRUE (carries_attached_group(*parent, key_property.get()));
    EXPECT_FALSE(carries_attached_group(*child,  key_property.get()));
    EXPECT_FALSE(is_attached_property_listed(*child, strength_property.get()));
}

TEST(Attached_group, a_style_supplying_the_key_gives_the_feature)
{
    std::shared_ptr<Test_object> style  = std::make_shared<Test_object>(type_a());
    Test_object                  holder{type_a()};
    style->set_value(key_property, true);

    EXPECT_TRUE(holder.set_style(style));
    EXPECT_EQ(holder.get_value_source(key_property.get()), Value_source::style);
    EXPECT_TRUE(carries_attached_group(holder, key_property.get()));
    EXPECT_TRUE(is_attached_property_listed(holder, strength_property.get()));

    EXPECT_TRUE(holder.set_style(nullptr));
    EXPECT_FALSE(carries_attached_group(holder, key_property.get()));
    EXPECT_FALSE(is_attached_property_listed(holder, strength_property.get()));
}

TEST(Attached_group, the_group_is_listed_on_its_holder_type_only)
{
    Test_object other{type_b()};
    other.set_value(key_property, true); // not a holder: the write does not reach a row

    EXPECT_FALSE(is_attached_property_listed(other, key_property.get()));
    EXPECT_FALSE(is_attached_property_listed(other, strength_property.get()));

    const std::vector<const Dependency_property*> candidates = collect_addable(other);
    EXPECT_FALSE(contains(candidates, key_property.get()));
    EXPECT_FALSE(contains(candidates, strength_property.get()));
}
