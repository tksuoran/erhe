// The Layout value group as attached properties of the node
// (doc/erhe/property_system.md section 4.13): `Layout.type` is the key
// property, `read_layout` is the record every consumer reads, and the
// container values inherit so an ancestor node holds them for the layout
// nodes below it (D30).

#include "erhe_scene/layout.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_string.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

using namespace erhe::property;
using erhe::scene::Axis_direction;
using erhe::scene::Layout;
using erhe::scene::Layout_data;
using erhe::scene::Layout_type;
using erhe::scene::Xform;
using erhe::scene::carries_layout;
using erhe::scene::read_layout;

namespace {

[[nodiscard]] auto make_layout_node(const Layout_type type) -> std::shared_ptr<Xform>
{
    std::shared_ptr<Xform> node = std::make_shared<Xform>("l");
    node->set_value(Layout::type_property, type);
    return node;
}

} // anonymous namespace

TEST(Layout_properties, a_node_carries_no_layout_until_the_key_is_set)
{
    std::shared_ptr<Xform> node = std::make_shared<Xform>("n");
    EXPECT_EQ(node->get_value(Layout::type_property), Layout_type::none);
    EXPECT_FALSE(carries_layout(*node));
    EXPECT_FALSE(read_layout(*node).has_value());

    node->set_value(Layout::type_property, Layout_type::stack);
    EXPECT_TRUE(carries_layout(*node));
    ASSERT_TRUE(read_layout(*node).has_value());

    node->clear_value(Layout::type_property);
    EXPECT_FALSE(carries_layout(*node));
    EXPECT_FALSE(read_layout(*node).has_value());
}

TEST(Layout_properties, defaults_match_previous_initializers)
{
    const std::shared_ptr<Xform> node = make_layout_node(Layout_type::stack);
    const std::optional<Layout_data> data = read_layout(*node);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data.value().type, Layout_type::stack);
    EXPECT_EQ(data.value().volume.min, (glm::vec3{-0.5f, -0.5f, -0.5f}));
    EXPECT_EQ(data.value().volume.max, (glm::vec3{0.5f, 0.5f, 0.5f}));
    EXPECT_EQ(data.value().primary,   Axis_direction::pos_x);
    EXPECT_EQ(data.value().secondary, Axis_direction::pos_y);
    EXPECT_EQ(data.value().tertiary,  Axis_direction::pos_z);
    EXPECT_EQ(data.value().gap, glm::vec3{0.0f});
    EXPECT_EQ(data.value().grid_track_count, (glm::ivec3{1, 1, 1}));
    EXPECT_EQ(node->get_value_source(Layout::gap_property), Value_source::default_value);
}

TEST(Layout_properties, values_written_on_the_node_reach_the_record)
{
    const std::shared_ptr<Xform> node = make_layout_node(Layout_type::grid);
    node->set_value(Layout::volume_min_property, glm::vec3{-1.0f});
    node->set_value(Layout::volume_max_property, glm::vec3{2.0f});
    node->set_value(Layout::primary_property, Axis_direction::neg_z);
    node->set_value(Layout::gap_property, glm::vec3{0.1f, 0.2f, 0.3f});
    node->set_value(Layout::grid_track_count_property, glm::ivec3{3, 2, 1});

    EXPECT_EQ(node->get_value_source(Layout::type_property), Value_source::local);
    const std::optional<Layout_data> data = read_layout(*node);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data.value().type, Layout_type::grid);
    EXPECT_EQ(data.value().volume.min, glm::vec3{-1.0f});
    EXPECT_EQ(data.value().volume.max, glm::vec3{2.0f});
    EXPECT_EQ(data.value().primary, Axis_direction::neg_z);
    EXPECT_EQ(data.value().gap, (glm::vec3{0.1f, 0.2f, 0.3f}));
    EXPECT_EQ(data.value().grid_track_count, (glm::ivec3{3, 2, 1}));

    // The validate callback keeps at least one track per axis.
    EXPECT_FALSE(node->set_value(Layout::grid_track_count_property.get(), Property_value{glm::ivec3{0, 1, 1}}));
    EXPECT_EQ(node->get_value(Layout::grid_track_count_property), (glm::ivec3{3, 2, 1}));

    node->clear_value(Layout::gap_property);
    EXPECT_EQ(node->get_value(Layout::gap_property), glm::vec3{0.0f});
}

TEST(Layout_properties, untyped_access_with_enumeration_labels)
{
    const std::shared_ptr<Xform> node = make_layout_node(Layout_type::stack);
    const Dependency_property* type = Property_registry::get().find_for_object(*node, "Layout.type");
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(to_string(*type, node->get_value(*type)), "Stack");
    ASSERT_TRUE(node->set_value(*type, parse_value(*type, "Flow").value()));
    EXPECT_EQ(node->get_value(Layout::type_property), Layout_type::flow);
    ASSERT_TRUE(node->set_value(*type, parse_value(*type, "None").value()));
    EXPECT_FALSE(carries_layout(*node));

    const Dependency_property* primary = Property_registry::get().find_for_object(*node, "Layout.primary");
    ASSERT_NE(primary, nullptr);
    ASSERT_TRUE(node->set_value(*primary, parse_value(*primary, "-Y").value()));
    EXPECT_EQ(node->get_value(Layout::primary_property), Axis_direction::neg_y);
    EXPECT_FALSE(parse_value(*primary, "W").has_value());
}

TEST(Layout_properties, an_ancestor_node_holds_values_for_the_layout_nodes_below_it)
{
    std::shared_ptr<Xform> holder = std::make_shared<Xform>("holder");
    std::shared_ptr<Xform> node   = make_layout_node(Layout_type::stack);
    node->set_parent(holder);

    holder->set_value(Layout::gap_property, glm::vec3{0.5f});
    EXPECT_EQ(node->get_value_source(Layout::gap_property), Value_source::inherited);
    EXPECT_EQ(read_layout(*node).value().gap, glm::vec3{0.5f});

    // The key does not inherit: the holder is no layout node of its own and
    // a child of a layout node does not become one.
    holder->set_value(Layout::type_property, Layout_type::grid);
    EXPECT_EQ(node->get_value(Layout::type_property), Layout_type::stack);
    node->clear_value(Layout::type_property);
    EXPECT_FALSE(carries_layout(*node));

    node->set_value(Layout::type_property, Layout_type::stack);
    node->set_value(Layout::gap_property, glm::vec3{0.25f}); // a local value shadows the holder
    EXPECT_EQ(read_layout(*node).value().gap, glm::vec3{0.25f});
    node->clear_value(Layout::gap_property);
    EXPECT_EQ(read_layout(*node).value().gap, glm::vec3{0.5f});
    holder->clear_value(Layout::gap_property);
    EXPECT_EQ(read_layout(*node).value().gap, glm::vec3{0.0f});
}

TEST(Layout_properties, clone_copies_the_values)
{
    const std::shared_ptr<Xform> node = make_layout_node(Layout_type::flow);
    node->set_value(Layout::gap_property, glm::vec3{1.0f});
    node->set_value(Layout::grid_track_count_property, glm::ivec3{2, 1, 1});
    node->set_value(Layout::grid_track_extent_x_property, std::vector<float>{1.0f, 2.0f});

    const std::shared_ptr<erhe::Item_base> clone_item = node->clone();
    const std::shared_ptr<Xform> clone = std::dynamic_pointer_cast<Xform>(clone_item);
    ASSERT_TRUE(clone);
    EXPECT_TRUE(carries_layout(*clone));
    EXPECT_EQ(read_layout(*clone).value().type, Layout_type::flow);
    EXPECT_EQ(read_layout(*clone).value().gap, glm::vec3{1.0f});
    EXPECT_EQ(clone->get_value_source(Layout::gap_property), Value_source::local);
    EXPECT_EQ(read_layout(*clone).value().grid_track_extent[0].size(), std::size_t{2});
    EXPECT_EQ(Property_set::read_local_values(*clone), Property_set::read_local_values(*node));
}

TEST(Layout_properties, grid_track_extent_is_coerced_to_the_track_count)
{
    const std::shared_ptr<Xform> node = make_layout_node(Layout_type::grid);
    node->set_value(Layout::grid_track_count_property, glm::ivec3{3, 1, 1});

    // An empty list means uniform tracks and is left alone.
    EXPECT_TRUE(node->get_value(Layout::grid_track_extent_x_property).empty());
    EXPECT_EQ(node->get_value_source(Layout::grid_track_extent_x_property), Value_source::default_value);

    // A longer list is cut to the track count, a shorter one padded.
    node->set_value(Layout::grid_track_extent_x_property, std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f, 5.0f});
    EXPECT_EQ(node->get_value(Layout::grid_track_extent_x_property), (std::vector<float>{1.0f, 2.0f, 3.0f}));
    EXPECT_TRUE(node->is_coerced(Layout::grid_track_extent_x_property));

    node->set_value(Layout::grid_track_extent_x_property, std::vector<float>{1.0f});
    EXPECT_EQ(node->get_value(Layout::grid_track_extent_x_property), (std::vector<float>{1.0f, 0.0f, 0.0f}));

    // A track count change re-coerces the stored list (change driven).
    node->set_value(Layout::grid_track_count_property, glm::ivec3{4, 1, 1});
    EXPECT_EQ(node->get_value(Layout::grid_track_extent_x_property).size(), std::size_t{4});
    node->set_value(Layout::grid_track_count_property, glm::ivec3{2, 1, 1});
    EXPECT_EQ(node->get_value(Layout::grid_track_extent_x_property), (std::vector<float>{1.0f, 0.0f}));

    // Back to uniform tracks.
    node->set_value(Layout::grid_track_extent_x_property, std::vector<float>{});
    EXPECT_TRUE(node->get_value(Layout::grid_track_extent_x_property).empty());
}

TEST(Layout_properties, a_holder_that_is_no_layout_node_keeps_the_list_as_authored)
{
    std::shared_ptr<Xform> holder = std::make_shared<Xform>("holder");
    std::shared_ptr<Xform> node   = make_layout_node(Layout_type::grid);
    node->set_parent(holder);
    node->set_value(Layout::grid_track_count_property, glm::ivec3{2, 1, 1});

    // The holder has no track count of its own to size against, so it keeps
    // the list as authored; the layout node reading it coerces its own copy.
    holder->set_value(Layout::grid_track_extent_x_property, std::vector<float>{1.0f, 2.0f, 3.0f});
    EXPECT_EQ(holder->get_value(Layout::grid_track_extent_x_property), (std::vector<float>{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(node->get_value_source(Layout::grid_track_extent_x_property), Value_source::inherited);
    EXPECT_EQ(read_layout(*node).value().grid_track_extent[0], (std::vector<float>{1.0f, 2.0f}));

    holder->clear_value(Layout::grid_track_extent_x_property);
    EXPECT_TRUE(read_layout(*node).value().grid_track_extent[0].empty());
}

TEST(Layout_properties, grid_track_extent_round_trips_as_text)
{
    const std::shared_ptr<Xform> node = make_layout_node(Layout_type::grid);
    node->set_value(Layout::grid_track_count_property, glm::ivec3{3, 1, 1});
    const Dependency_property& property = Layout::grid_track_extent_x_property.get();
    ASSERT_TRUE(node->set_value(property, parse_value(property, "1 2 3").value()));
    EXPECT_EQ(node->get_value(Layout::grid_track_extent_x_property), (std::vector<float>{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(to_string(property, node->get_value(property)), "1 2 3");
    ASSERT_TRUE(node->set_value(property, parse_value(property, "").value()));
    EXPECT_TRUE(node->get_value(Layout::grid_track_extent_x_property).empty());
}
