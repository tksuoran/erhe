// Ik_settings fields as entry-stored erhe::property properties
// (doc/erhe/property_system.md section 4.19): get_data() is a mirror of the
// effective values, every field but rest_rotation inherits, and a node
// holds them for the attachments below it (D30).

#include "scene/node_ik_settings.hpp"

#include "erhe_property/dependency_property.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;
using editor::Ik_settings;
using editor::Ik_settings_data;
using erhe::scene::Xform;

TEST(Ik_settings_properties, defaults_match_data_defaults)
{
    auto ik_settings = std::make_shared<Ik_settings>("ik");
    EXPECT_EQ(ik_settings->get_data(), Ik_settings_data{});
    EXPECT_FALSE(ik_settings->get_value(Ik_settings::limit_x_property));
    EXPECT_EQ(ik_settings->get_value(Ik_settings::limit_min_property), glm::vec3{-glm::pi<float>()});
    EXPECT_EQ(ik_settings->get_value_source(Ik_settings::limit_max_property), Value_source::default_value);
    EXPECT_TRUE (Ik_settings::lock_x_property.get().get_metadata(Ik_settings::property_owner_type()).inherits);
    EXPECT_FALSE(Ik_settings::rest_rotation_property.get().get_metadata(Ik_settings::property_owner_type()).inherits);
}

TEST(Ik_settings_properties, setters_reach_the_mirror_and_limits_are_clamped)
{
    auto ik_settings = std::make_shared<Ik_settings>("ik");
    ik_settings->set_lock(1, true);
    ik_settings->set_limit(2, true);
    ik_settings->set_limit_min(glm::vec3{-4.0f, -0.5f, 1.0f});
    ik_settings->set_limit_max(glm::vec3{0.5f, 4.0f, -1.0f});
    ik_settings->set_stiffness(glm::vec3{2.0f, 0.5f, -1.0f});
    const glm::quat rest = glm::angleAxis(0.5f, glm::vec3{0.0f, 0.0f, 1.0f});
    ik_settings->set_rest_rotation(rest);

    const Ik_settings_data& data = ik_settings->get_data();
    EXPECT_FALSE(data.lock[0]);
    EXPECT_TRUE (data.lock[1]);
    EXPECT_TRUE (data.limit[2]);
    EXPECT_EQ(data.limit_min, (glm::vec3{-glm::pi<float>(), -0.5f, 0.0f}));
    EXPECT_EQ(data.limit_max, (glm::vec3{0.5f, glm::pi<float>(), 0.0f}));
    EXPECT_EQ(data.stiffness, (glm::vec3{0.99f, 0.5f, 0.0f}));
    EXPECT_EQ(data.rest_rotation, rest);
    EXPECT_EQ(ik_settings->get_value_source(Ik_settings::rest_rotation_property), Value_source::local);

    ik_settings->clear_value(Ik_settings::lock_y_property);
    EXPECT_FALSE(data.lock[1]);
}

TEST(Ik_settings_properties, node_held_values_are_inherited_into_the_mirror)
{
    auto node        = std::make_shared<Xform>("n");
    auto ik_settings = std::make_shared<Ik_settings>("ik");
    node->attach(ik_settings);
    const Property_registry& registry = Property_registry::get();
    EXPECT_TRUE(registry.is_secondary_property(*node, Ik_settings::limit_x_property.get()));
    EXPECT_EQ(registry.find_for_object(*node, "Ik_settings.limit_x"), Ik_settings::limit_x_property.get_ptr());

    node->set_value(Ik_settings::limit_x_property, true);
    EXPECT_EQ(ik_settings->get_value_source(Ik_settings::limit_x_property), Value_source::inherited);
    EXPECT_TRUE(ik_settings->get_data().limit[0]);

    ik_settings->set_limit(0, false); // a local value shadows the node
    EXPECT_FALSE(ik_settings->get_data().limit[0]);
    ik_settings->clear_value(Ik_settings::limit_x_property);
    EXPECT_TRUE(ik_settings->get_data().limit[0]);
    node->clear_value(Ik_settings::limit_x_property);
    EXPECT_FALSE(ik_settings->get_data().limit[0]);

    // rest_rotation does not inherit: a node-held value stays on the node.
    const glm::quat rest = glm::angleAxis(0.5f, glm::vec3{1.0f, 0.0f, 0.0f});
    node->set_value(Ik_settings::rest_rotation_property, rest);
    EXPECT_EQ(ik_settings->get_value_source(Ik_settings::rest_rotation_property), Value_source::default_value);
    EXPECT_EQ(ik_settings->get_data().rest_rotation, Ik_settings_data{}.rest_rotation);
}

TEST(Ik_settings_properties, clone_copies_mirror_and_store)
{
    auto ik_settings = std::make_shared<Ik_settings>("ik");
    ik_settings->set_lock(0, true);
    ik_settings->set_limit_max(glm::vec3{1.0f});
    const auto clone = std::static_pointer_cast<Ik_settings>(ik_settings->clone());
    ASSERT_TRUE(clone);
    EXPECT_TRUE(clone->get_data().lock[0]);
    EXPECT_EQ(clone->get_data().limit_max, glm::vec3{1.0f});
    EXPECT_EQ(clone->get_value_source(Ik_settings::lock_x_property), Value_source::local);
    EXPECT_EQ(clone->get_value_source(Ik_settings::limit_min_property), Value_source::default_value);
}

// doc/plans/rigging/pole_target.md R1-R4: the pole node bridged over a weak
// member, the angle in the entry store and in the mirror.
TEST(Ik_settings_properties, pole_defaults_and_setters)
{
    auto ik_settings = std::make_shared<Ik_settings>("ik");
    EXPECT_EQ(ik_settings->get_data().pole_angle, 0.0f);
    EXPECT_EQ(ik_settings->get_pole_target(), nullptr);
    EXPECT_EQ(ik_settings->get_value_source(Ik_settings::pole_angle_property), Value_source::default_value);
    EXPECT_TRUE (Ik_settings::pole_angle_property.get().get_metadata(Ik_settings::property_owner_type()).inherits);
    EXPECT_FALSE(Ik_settings::pole_target_property.get().get_metadata(Ik_settings::property_owner_type()).inherits);

    // The angle is periodic, so it is NOT coerced into the drag range (R17).
    ik_settings->set_pole_angle(4.0f);
    EXPECT_EQ(ik_settings->get_data().pole_angle, 4.0f);
    EXPECT_EQ(ik_settings->get_value(Ik_settings::pole_angle_property), 4.0f);
    ik_settings->clear_value(Ik_settings::pole_angle_property);
    EXPECT_EQ(ik_settings->get_data().pole_angle, 0.0f);

    auto pole = std::make_shared<Xform>("pole");
    ik_settings->set_pole_target(pole);
    EXPECT_EQ(ik_settings->get_pole_target(), pole);
    // The bridge reads and writes that same weak member.
    EXPECT_EQ(ik_settings->get_value(Ik_settings::pole_target_property).object.get(), pole.get());
    ik_settings->set_value(Ik_settings::pole_target_property, Object_reference{});
    EXPECT_EQ(ik_settings->get_pole_target(), nullptr);
}

// R2: the reference is weak, so it neither owns the pole nor survives it, and
// a clone keeps naming the same node.
TEST(Ik_settings_properties, pole_reference_is_weak_and_clones)
{
    auto ik_settings = std::make_shared<Ik_settings>("ik");
    auto pole = std::make_shared<Xform>("pole");
    ik_settings->set_pole_target(pole);

    const auto clone = std::static_pointer_cast<Ik_settings>(ik_settings->clone());
    ASSERT_TRUE(clone);
    EXPECT_EQ(clone->get_pole_target(), pole);

    const std::weak_ptr<Xform> watch = pole;
    pole.reset();
    EXPECT_TRUE(watch.expired()); // neither attachment kept the node alive
    EXPECT_EQ(ik_settings->get_pole_target(), nullptr);
    EXPECT_EQ(clone->get_pole_target(), nullptr);
}

// R1: validate is the node traits' own, so a non-Node item is refused with the
// property's error rather than stored.
TEST(Ik_settings_properties, pole_target_refuses_a_non_node_item)
{
    auto ik_settings = std::make_shared<Ik_settings>("ik");
    auto not_a_node  = std::make_shared<Ik_settings>("also not a node");
    const bool accepted = ik_settings->set_value(
        Ik_settings::pole_target_property.get(),
        Object_reference{not_a_node}
    );
    EXPECT_FALSE(accepted);
    EXPECT_EQ(ik_settings->get_pole_target(), nullptr);
}
