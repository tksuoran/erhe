// Per-object defaults (doc/property-system.md D31): the default layer of a
// property is Property_metadata::compute_default when it is bound, so an
// object derives its own default from state it already holds while every
// authored layer above it still wins.

#include "test_object.hpp"

#include <gtest/gtest.h>

using namespace erhe::property;
using namespace erhe::property::test;

namespace {

// An object whose default "mode" follows a plain member.
class Defaulted_object : public Test_object
{
public:
    Defaulted_object() : Test_object{type_c()} {}

    int mode_seed{0};

    // The library's protected refresh hook, for the test to drive after the
    // seed changed (Item_base::set_flag_bits does this around the flag bits
    // its purpose default is derived from).
    void refresh(const Dependency_property& property, const Property_value& old_value, const Value_source old_source)
    {
        refresh_computed_default(property, old_value, old_source);
    }
};

const Property<int> defaulted_mode = Property<int>::register_property(
    "defaulted_mode", type_c(),
    Property_metadata{
        .default_value   = 7,
        .inherits        = true,
        .compute_default = [](const Dependency_object& object) -> Property_value {
            return static_cast<const Defaulted_object&>(object).mode_seed * 10;
        }
    }
);

} // namespace

TEST(Computed_default, default_layer_is_per_object)
{
    Defaulted_object a{};
    Defaulted_object b{};
    b.mode_seed = 3;
    EXPECT_EQ(a.get_value(defaulted_mode), 0);
    EXPECT_EQ(b.get_value(defaulted_mode), 30);
    EXPECT_EQ(a.get_value_source(defaulted_mode.get()), Value_source::default_value);
    EXPECT_EQ(b.get_value_source(defaulted_mode.get()), Value_source::default_value);
    EXPECT_EQ(a.get_default_value(defaulted_mode.get()), Property_value{0});
    EXPECT_EQ(b.get_default_value(defaulted_mode.get()), Property_value{30});
}

TEST(Computed_default, local_value_wins_over_the_computed_default)
{
    Defaulted_object object{};
    object.mode_seed = 3;
    object.set_value(defaulted_mode, 5);
    EXPECT_EQ(object.get_value(defaulted_mode), 5);
    EXPECT_EQ(object.get_value_source(defaulted_mode.get()), Value_source::local);

    EXPECT_TRUE(object.clear_value(defaulted_mode.get()));
    EXPECT_EQ(object.get_value(defaulted_mode), 30);
    EXPECT_EQ(object.get_value_source(defaulted_mode.get()), Value_source::default_value);
}

TEST(Computed_default, inherited_value_wins_over_the_computed_default)
{
    Defaulted_object parent{};
    Defaulted_object child{};
    child.set_parent(&parent);
    child.mode_seed = 3;
    EXPECT_EQ(child.get_value(defaulted_mode), 30);

    parent.set_value(defaulted_mode, 5);
    EXPECT_EQ(child.get_value(defaulted_mode), 5);
    EXPECT_EQ(child.get_value_source(defaulted_mode.get()), Value_source::inherited);

    parent.clear_value(defaulted_mode.get());
    EXPECT_EQ(child.get_value(defaulted_mode), 30);
    EXPECT_EQ(child.get_value_source(defaulted_mode.get()), Value_source::default_value);
}

TEST(Computed_default, refresh_notifies_this_object_only)
{
    Defaulted_object parent{};
    Defaulted_object child{};
    child.set_parent(&parent);
    parent.changes.clear();
    child.changes.clear();

    const Property_value old_value  = parent.get_value(defaulted_mode.get());
    const Value_source   old_source = parent.get_value_source(defaulted_mode.get());
    parent.mode_seed = 4;
    parent.refresh(defaulted_mode.get(), old_value, old_source);

    ASSERT_EQ(parent.changes.size(), std::size_t{1});
    EXPECT_EQ(parent.changes[0].property_name, "defaulted_mode");
    EXPECT_EQ(parent.changes[0].old_value, Property_value{0});
    EXPECT_EQ(parent.changes[0].new_value, Property_value{40});
    EXPECT_EQ(parent.changes[0].new_source, Value_source::default_value);
    // A default is below every inherited layer, so the child's own default
    // is unchanged and it is not notified.
    EXPECT_TRUE(child.changes.empty());
    EXPECT_EQ(child.get_value(defaulted_mode), 0);
}

TEST(Computed_default, refresh_of_an_unchanged_default_notifies_nothing)
{
    Defaulted_object object{};
    object.changes.clear();
    const Property_value old_value  = object.get_value(defaulted_mode.get());
    const Value_source   old_source = object.get_value_source(defaulted_mode.get());
    object.refresh(defaulted_mode.get(), old_value, old_source);
    EXPECT_TRUE(object.changes.empty());
}
