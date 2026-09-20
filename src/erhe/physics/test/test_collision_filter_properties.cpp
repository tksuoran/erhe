#include "erhe_physics/collision_filter.hpp"

#include "erhe_property/property_string.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

// The three system name lists of a Collision_filter are entry-stored
// `string[]` properties (doc/erhe/property_system.md section 4.21): the
// mirrors the backends read follow every source of a change, and the value
// crosses a file as the D16 quoted list.
namespace {

using erhe::physics::Collision_filter;

} // anonymous namespace

TEST(Collision_filter_properties, defaults_are_empty_lists)
{
    const std::shared_ptr<Collision_filter> filter = std::make_shared<Collision_filter>("filter");
    EXPECT_TRUE(filter->get_collision_systems().empty());
    EXPECT_TRUE(filter->get_collide_with_systems().empty());
    EXPECT_TRUE(filter->get_not_collide_with_systems().empty());
    EXPECT_EQ(filter->get_value_source(Collision_filter::collision_systems_property.get()), erhe::property::Value_source::default_value);
}

TEST(Collision_filter_properties, setter_reaches_the_mirror)
{
    const std::shared_ptr<Collision_filter> filter = std::make_shared<Collision_filter>("filter");
    filter->set_collision_systems({"props", "debris"});
    ASSERT_EQ(filter->get_collision_systems().size(), 2u);
    EXPECT_EQ(filter->get_collision_systems()[0], "props");
    EXPECT_EQ(filter->get_collision_systems()[1], "debris");

    filter->set_collide_with_systems({"terrain"});
    ASSERT_EQ(filter->get_collide_with_systems().size(), 1u);
    EXPECT_EQ(filter->get_collide_with_systems().front(), "terrain");

    filter->set_not_collide_with_systems({"characters"});
    ASSERT_EQ(filter->get_not_collide_with_systems().size(), 1u);
    EXPECT_EQ(filter->get_not_collide_with_systems().front(), "characters");
}

// The untyped path every generic writer takes (MCP set_item_property, a
// property serializer, Property_set_operation).
TEST(Collision_filter_properties, untyped_write_reaches_the_mirror)
{
    const std::shared_ptr<Collision_filter> filter = std::make_shared<Collision_filter>("filter");
    const erhe::property::Dependency_property* const property =
        erhe::property::Property_registry::get().find_for_object(*filter, "collision_systems");
    ASSERT_NE(property, nullptr);
    EXPECT_EQ(property->get_type(), erhe::property::Property_type::string_array);

    const std::optional<erhe::property::Property_value> parsed =
        erhe::property::parse_value(*property, "\"a\" \"b c\"");
    ASSERT_TRUE(parsed.has_value());
    filter->set_value(*property, parsed.value());
    ASSERT_EQ(filter->get_collision_systems().size(), 2u);
    EXPECT_EQ(filter->get_collision_systems()[1], "b c");
    EXPECT_EQ(erhe::property::to_string(*property, filter->get_value(*property)), "\"a\" \"b c\"");
}

TEST(Collision_filter_properties, clone_carries_the_lists)
{
    const std::shared_ptr<Collision_filter> filter = std::make_shared<Collision_filter>("filter");
    filter->set_collision_systems({"props"});
    filter->set_not_collide_with_systems({"characters"});

    const Collision_filter clone{*filter.get()};
    ASSERT_EQ(clone.get_collision_systems().size(), 1u);
    EXPECT_EQ(clone.get_collision_systems().front(), "props");
    ASSERT_EQ(clone.get_not_collide_with_systems().size(), 1u);
    EXPECT_EQ(clone.get_not_collide_with_systems().front(), "characters");
}

// A filter states the systems of its own body, so no list inherits.
TEST(Collision_filter_properties, lists_do_not_inherit)
{
    const erhe::property::Owner_type owner = Collision_filter::property_owner_type();
    EXPECT_FALSE(Collision_filter::collision_systems_property.get_ptr()->get_metadata(owner).inherits);
    EXPECT_FALSE(Collision_filter::collide_with_systems_property.get_ptr()->get_metadata(owner).inherits);
    EXPECT_FALSE(Collision_filter::not_collide_with_systems_property.get_ptr()->get_metadata(owner).inherits);
}
