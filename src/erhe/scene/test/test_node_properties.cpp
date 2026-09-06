// Node translation / rotation / scale as bridged erhe::property properties
// (doc/property-system.md section 4.2): reads see the transform,
// writes update the world transform and the node's transform serial.

#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_string.hpp"

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;

namespace {

auto approx(const glm::vec3& a, const glm::vec3& b, const float eps = 1e-5f) -> bool
{
    return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3{eps}));
}

} // anonymous namespace

TEST(Node_properties, reads_reflect_the_transform)
{
    auto node = std::make_shared<erhe::scene::Xform>("n");
    EXPECT_EQ(node->get_value(erhe::scene::Node::translation_property), glm::vec3{0.0f});
    EXPECT_EQ(node->get_value(erhe::scene::Node::scale_property), glm::vec3{1.0f});
    EXPECT_EQ(node->get_value_source(erhe::scene::Node::translation_property), Value_source::local);

    erhe::scene::Trs_transform t;
    t.set_trs(glm::vec3{1.0f, 2.0f, 3.0f}, glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f}), glm::vec3{2.0f});
    node->set_parent_from_node(t);
    EXPECT_EQ(node->get_value(erhe::scene::Node::translation_property), (glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(node->get_value(erhe::scene::Node::scale_property), glm::vec3{2.0f});
    const glm::quat q = node->get_value(erhe::scene::Node::rotation_property);
    EXPECT_NEAR(glm::angle(q), glm::half_pi<float>(), 1e-5f);
}

TEST(Node_properties, writes_update_world_transform_and_serial)
{
    auto parent = std::make_shared<erhe::scene::Xform>("parent");
    auto child  = std::make_shared<erhe::scene::Xform>("child");
    child->set_parent(parent);
    parent->set_value(erhe::scene::Node::translation_property, glm::vec3{10.0f, 0.0f, 0.0f});
    const uint64_t serial_before = child->node_data.transforms.parent_from_node_serial;

    child->set_value(erhe::scene::Node::translation_property, glm::vec3{1.0f, 0.0f, 0.0f});
    EXPECT_GT(child->node_data.transforms.parent_from_node_serial, serial_before);
    EXPECT_TRUE(approx(glm::vec3{child->position_in_world()}, glm::vec3{11.0f, 0.0f, 0.0f}));

    child->set_value(erhe::scene::Node::scale_property, glm::vec3{2.0f, 2.0f, 2.0f});
    EXPECT_TRUE(approx(child->transform_point_from_local_to_world(glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{13.0f, 0.0f, 0.0f}));

    child->set_value(erhe::scene::Node::rotation_property, glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f}));
    EXPECT_TRUE(approx(child->transform_point_from_local_to_world(glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{11.0f, 2.0f, 0.0f}));

    // clear_value() on a bridged property writes the default (identity).
    child->clear_value(erhe::scene::Node::scale_property);
    EXPECT_EQ(child->get_value(erhe::scene::Node::scale_property), glm::vec3{1.0f});
}

TEST(Node_properties, untyped_access_and_bag)
{
    auto node = std::make_shared<erhe::scene::Xform>("n");
    const Dependency_property* translation = Property_registry::get().find_for_object(node->get_property_owner_type(), "translation");
    ASSERT_NE(translation, nullptr);
    node->set_value(*translation, parse_value(*translation, "1 2 3").value());
    EXPECT_EQ(to_string(*translation, node->get_value(*translation)), "1 2 3");

    const Property_set bag = Property_set::read_local_values(*node);
    EXPECT_TRUE(bag.contains(erhe::scene::Node::translation_property));
    EXPECT_TRUE(bag.contains(erhe::scene::Node::rotation_property));
    EXPECT_TRUE(bag.contains(erhe::scene::Node::scale_property));

    auto other = std::make_shared<erhe::scene::Xform>("other");
    bag.apply(*other);
    EXPECT_EQ(other->get_value(erhe::scene::Node::translation_property), (glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_TRUE(approx(glm::vec3{other->position_in_world()}, glm::vec3{1.0f, 2.0f, 3.0f}));
}
