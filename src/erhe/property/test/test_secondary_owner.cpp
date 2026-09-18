// Secondary owner type (doc/property_system.md D30): an object that names
// a second owner type holds that type's non-bridged properties by qualified
// name, and its inheritance descendants of that type read them.

#include "test_object.hpp"

#include <gtest/gtest.h>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

const Property<float> sec_tint  = Property<float>::register_property("sec_tint", type_b(), Property_metadata{.default_value = 1.0f, .inherits = true});
const Property<float> sec_plain = Property<float>::register_property("sec_plain", type_b(), Property_metadata{.default_value = 3.0f});

// A property of a descendant of the secondary type: a holder of type_b
// values holds it too (a node holds the values of every attachment class).
// Its property_changed callback belongs to type_b_child objects.
const Property<float> sec_child_computed = Property<float>::register_computed(
    "sec_child_computed", type_b_child(),
    [](const Dependency_object& object) -> Property_value {
        EXPECT_EQ(object.get_property_owner_type(), type_b_child());
        return 1.0f;
    },
    Property_metadata{}
);
int                   sec_child_callbacks = 0;
const Property<float> sec_child = Property<float>::register_property(
    "sec_child", type_b_child(),
    Property_metadata{
        .default_value    = 7.0f,
        .property_changed = [](Dependency_object& object, const Property_changed_args&) {
            EXPECT_EQ(object.get_property_owner_type(), type_b_child());
            ++sec_child_callbacks;
        },
        .inherits         = true
    }
);

// A type_a object that also holds type_b properties (a folder of type_b entries).
class Folder_object : public Test_object
{
public:
    Folder_object() : Test_object{type_a()} {}
    auto get_secondary_property_owner_type() const -> std::optional<Owner_type> override { return type_b(); }
};

} // anonymous namespace

TEST(Secondary_owner, lookup_naming_and_listing)
{
    const Property_registry& registry = Property_registry::get();
    Folder_object folder;
    Test_object   plain{type_a()};

    EXPECT_TRUE (registry.is_secondary_property(folder, sec_tint.get()));
    EXPECT_FALSE(registry.is_secondary_property(plain,  sec_tint.get()));

    // Qualified on the folder, plain on a type_b object.
    Test_object entry{type_b()};
    EXPECT_EQ(registry.qualified_name(folder, sec_tint.get()), "type_b.sec_tint");
    EXPECT_EQ(registry.qualified_name(entry,  sec_tint.get()), "sec_tint");

    EXPECT_EQ(registry.find_for_object(folder, "type_b.sec_tint"), sec_tint.get_ptr());
    EXPECT_EQ(registry.find_for_object(folder, "sec_tint"),        nullptr); // not on the folder's own chain
    EXPECT_EQ(registry.find_for_object(plain,  "type_b.sec_tint"), nullptr); // no secondary type

    std::vector<const Dependency_property*> listed;
    registry.for_each_secondary_property(folder, [&listed](const Dependency_property& p) { listed.push_back(&p); });
    EXPECT_NE(std::find(listed.begin(), listed.end(), sec_tint.get_ptr()),  listed.end());
    EXPECT_NE(std::find(listed.begin(), listed.end(), sec_plain.get_ptr()), listed.end());
    EXPECT_NE(std::find(listed.begin(), listed.end(), sec_child.get_ptr()), listed.end());
    EXPECT_EQ(std::count(listed.begin(), listed.end(), sec_child.get_ptr()), 1);
    listed.clear();
    registry.for_each_secondary_property(plain, [&listed](const Dependency_property& p) { listed.push_back(&p); });
    EXPECT_TRUE(listed.empty());
}

TEST(Secondary_owner, folder_value_is_inherited_by_entries)
{
    Folder_object folder;
    Test_object   entry{type_b()};
    entry.set_parent(&folder);

    EXPECT_EQ(entry.get_value(sec_tint), 1.0f);
    folder.set_value(sec_tint, 0.25f);
    EXPECT_EQ(entry.get_value(sec_tint), 0.25f);
    EXPECT_EQ(entry.get_value_source(sec_tint.get()), Value_source::inherited);
    EXPECT_EQ(folder.get_value_source(sec_tint.get()), Value_source::local);

    // A local value on the entry shadows the folder; clearing it re-reads.
    entry.set_value(sec_tint, 0.5f);
    EXPECT_EQ(entry.get_value(sec_tint), 0.5f);
    entry.clear_value(sec_tint);
    EXPECT_EQ(entry.get_value(sec_tint), 0.25f);
    folder.clear_value(sec_tint);
    EXPECT_EQ(entry.get_value(sec_tint), 1.0f);
}

TEST(Secondary_owner, descendant_type_property_on_holder)
{
    const Property_registry& registry = Property_registry::get();
    Folder_object folder;
    Test_object   entry{type_b_child()};
    Test_object   other{type_b()};
    entry.set_parent(&folder);

    EXPECT_TRUE (registry.is_secondary_property(folder, sec_child.get()));
    EXPECT_FALSE(registry.is_secondary_property(other,  sec_child.get())); // no secondary type
    EXPECT_FALSE(registry.is_secondary_property(folder, sec_child_computed.get())); // computed: reads a type_b_child
    std::vector<const Dependency_property*> listed;
    registry.for_each_secondary_property(folder, [&listed](const Dependency_property& p) { listed.push_back(&p); });
    EXPECT_EQ(std::find(listed.begin(), listed.end(), sec_child_computed.get_ptr()), listed.end());
    EXPECT_EQ(registry.qualified_name(folder, sec_child.get()), "type_b_child.sec_child");
    EXPECT_EQ(registry.find_for_object(folder, "type_b_child.sec_child"), sec_child.get_ptr());

    // The registering class's property_changed runs for the entry, never
    // for the holder (which is not a type_b_child).
    sec_child_callbacks = 0;
    folder.set_value(sec_child, 2.0f);
    EXPECT_EQ(entry.get_value(sec_child), 2.0f);
    EXPECT_EQ(entry.get_value_source(sec_child.get()), Value_source::inherited);
    EXPECT_EQ(sec_child_callbacks, 1);
    folder.clear_value(sec_child);
    EXPECT_EQ(entry.get_value(sec_child), 7.0f);
    EXPECT_EQ(sec_child_callbacks, 2);
}
