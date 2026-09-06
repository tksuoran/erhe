// Layout parameters as entry-stored erhe::property properties
// (doc/property-system.md section 4.13): the members update() reads are a
// mirror of the effective values, every parameter inherits, and a node
// holds them for the layouts below it (D30).

#include "erhe_scene/layout.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_string.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;
using erhe::scene::Axis_direction;
using erhe::scene::Layout;
using erhe::scene::Layout_type;
using erhe::scene::Node;
using erhe::scene::Xform;

TEST(Layout_properties, defaults_match_previous_initializers)
{
    auto layout = std::make_shared<Layout>("l");
    EXPECT_EQ(layout->get_layout_type(), Layout_type::stack);
    EXPECT_EQ(layout->get_volume().min, (glm::vec3{-0.5f, -0.5f, -0.5f}));
    EXPECT_EQ(layout->get_volume().max, (glm::vec3{0.5f, 0.5f, 0.5f}));
    EXPECT_EQ(layout->get_primary(),   Axis_direction::pos_x);
    EXPECT_EQ(layout->get_secondary(), Axis_direction::pos_y);
    EXPECT_EQ(layout->get_tertiary(),  Axis_direction::pos_z);
    EXPECT_EQ(layout->get_gap(), glm::vec3{0.0f});
    EXPECT_EQ(layout->get_grid_track_count(), (glm::ivec3{1, 1, 1}));
    EXPECT_EQ(layout->get_value_source(Layout::gap_property), Value_source::default_value);
    EXPECT_FALSE(layout->has_local_value(Layout::type_property.get()));
}

TEST(Layout_properties, setters_write_the_store_and_the_mirror_follows)
{
    auto layout = std::make_shared<Layout>("l");
    layout->set_layout_type(Layout_type::grid);
    layout->set_volume_min(glm::vec3{-1.0f});
    layout->set_volume_max(glm::vec3{2.0f});
    layout->set_primary(Axis_direction::neg_z);
    layout->set_gap(glm::vec3{0.1f, 0.2f, 0.3f});
    layout->set_grid_track_count(glm::ivec3{3, 2, 1});
    EXPECT_EQ(layout->get_value_source(Layout::type_property), Value_source::local);
    EXPECT_EQ(layout->get_layout_type(), Layout_type::grid);
    EXPECT_EQ(layout->get_volume().min, glm::vec3{-1.0f});
    EXPECT_EQ(layout->get_volume().max, glm::vec3{2.0f});
    EXPECT_EQ(layout->get_primary(), Axis_direction::neg_z);
    EXPECT_EQ(layout->get_gap(), (glm::vec3{0.1f, 0.2f, 0.3f}));
    EXPECT_EQ(layout->get_grid_track_count(), (glm::ivec3{3, 2, 1}));

    // The validate callback keeps at least one track per axis.
    EXPECT_FALSE(layout->set_value(Layout::grid_track_count_property.get(), Property_value{glm::ivec3{0, 1, 1}}));
    EXPECT_EQ(layout->get_grid_track_count(), (glm::ivec3{3, 2, 1}));

    layout->clear_value(Layout::gap_property);
    EXPECT_EQ(layout->get_gap(), glm::vec3{0.0f});
}

TEST(Layout_properties, untyped_access_with_enumeration_labels)
{
    auto layout = std::make_shared<Layout>("l");
    const Dependency_property* type = Property_registry::get().find_for_object(layout->get_property_owner_type(), "type");
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(to_string(*type, layout->get_value(*type)), "Stack");
    ASSERT_TRUE(layout->set_value(*type, parse_value(*type, "Flow").value()));
    EXPECT_EQ(layout->get_layout_type(), Layout_type::flow);

    const Dependency_property* primary = Property_registry::get().find_for_object(layout->get_property_owner_type(), "primary");
    ASSERT_NE(primary, nullptr);
    ASSERT_TRUE(layout->set_value(*primary, parse_value(*primary, "-Y").value()));
    EXPECT_EQ(layout->get_primary(), Axis_direction::neg_y);
    EXPECT_FALSE(parse_value(*primary, "W").has_value());
}

TEST(Layout_properties, node_held_values_are_inherited_into_the_mirror)
{
    auto node   = std::make_shared<Xform>("n");
    auto layout = std::make_shared<Layout>("l");
    node->attach(layout);
    const Property_registry& registry = Property_registry::get();
    EXPECT_TRUE(registry.is_secondary_property(*node, Layout::gap_property.get()));
    EXPECT_EQ(registry.find_for_object(*node, "Layout.gap"), Layout::gap_property.get_ptr());

    node->set_value(Layout::gap_property, glm::vec3{0.5f});
    node->set_value(Layout::type_property, Layout_type::grid);
    EXPECT_EQ(layout->get_value_source(Layout::gap_property), Value_source::inherited);
    EXPECT_EQ(layout->get_gap(), glm::vec3{0.5f});
    EXPECT_EQ(layout->get_layout_type(), Layout_type::grid);

    layout->set_gap(glm::vec3{0.25f}); // a local value shadows the node
    EXPECT_EQ(layout->get_gap(), glm::vec3{0.25f});
    layout->clear_value(Layout::gap_property);
    EXPECT_EQ(layout->get_gap(), glm::vec3{0.5f});
    node->clear_value(Layout::gap_property);
    EXPECT_EQ(layout->get_gap(), glm::vec3{0.0f});
}

TEST(Layout_properties, clone_copies_the_store_and_the_mirror)
{
    auto layout = std::make_shared<Layout>("l");
    layout->set_layout_type(Layout_type::flow);
    layout->set_gap(glm::vec3{1.0f});
    layout->get_grid_track_extent(0) = {1.0f, 2.0f};
    const std::shared_ptr<erhe::Item_base> clone_item = layout->clone();
    const std::shared_ptr<Layout> clone = std::dynamic_pointer_cast<Layout>(clone_item);
    ASSERT_TRUE(clone);
    EXPECT_EQ(clone->get_layout_type(), Layout_type::flow);
    EXPECT_EQ(clone->get_gap(), glm::vec3{1.0f});
    EXPECT_EQ(clone->get_value_source(Layout::gap_property), Value_source::local);
    EXPECT_EQ(clone->get_grid_track_extent(0).size(), std::size_t{2});
    EXPECT_EQ(Property_set::read_local_values(*clone), Property_set::read_local_values(*layout));
}
