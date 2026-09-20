// Scene::ambient_light as an entry-stored erhe::property property
// (doc/erhe/property_system.md section 4.20): the private m_ambient_light
// member is a mirror of the effective value, the property does not inherit,
// and a style holds the value for the scenes that have no local one.

#include "erhe_scene/scene.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_string.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;
using erhe::scene::Scene;

TEST(Scene_properties, ambient_light_defaults_to_black)
{
    auto scene = std::make_shared<Scene>("s");
    EXPECT_EQ(scene->get_value_source(Scene::ambient_light_property), Value_source::default_value);
    EXPECT_EQ(scene->get_value(Scene::ambient_light_property), glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(scene->get_ambient_light(), glm::vec3(0.0f, 0.0f, 0.0f));
    EXPECT_FALSE(Scene::ambient_light_property.get().get_default_metadata().inherits);
}

TEST(Scene_properties, setter_reaches_the_mirror)
{
    auto scene = std::make_shared<Scene>("s");
    scene->set_ambient_light(glm::vec3{0.5f, 0.25f, 0.125f});
    EXPECT_EQ(scene->get_value_source(Scene::ambient_light_property), Value_source::local);
    EXPECT_EQ(scene->get_ambient_light(), glm::vec3(0.5f, 0.25f, 0.125f));

    scene->set_value(Scene::ambient_light_property, glm::vec3{1.0f, 0.0f, 0.0f});
    EXPECT_EQ(scene->get_ambient_light(), glm::vec3(1.0f, 0.0f, 0.0f));

    scene->clear_value(Scene::ambient_light_property);
    EXPECT_EQ(scene->get_value_source(Scene::ambient_light_property), Value_source::default_value);
    EXPECT_EQ(scene->get_ambient_light(), glm::vec3(0.0f, 0.0f, 0.0f));
}

TEST(Scene_properties, untyped_access_reaches_the_mirror)
{
    auto scene = std::make_shared<Scene>("s");
    const Dependency_property* property = Property_registry::get().find_for_object(scene->get_property_owner_type(), "ambient_light");
    ASSERT_NE(property, nullptr);
    scene->set_value(*property, parse_value(*property, "0.25 0.5 0.75").value());
    EXPECT_EQ(scene->get_ambient_light(), glm::vec3(0.25f, 0.5f, 0.75f));
}

TEST(Scene_properties, style_held_value_reaches_the_mirror)
{
    auto style = std::make_shared<Scene>("style");
    auto scene = std::make_shared<Scene>("s");
    style->set_ambient_light(glm::vec3{0.1f, 0.2f, 0.3f});
    EXPECT_TRUE(scene->set_style(style));
    EXPECT_EQ(scene->get_value_source(Scene::ambient_light_property), Value_source::style);
    EXPECT_EQ(scene->get_ambient_light(), glm::vec3(0.1f, 0.2f, 0.3f));

    scene->set_ambient_light(glm::vec3{0.9f, 0.9f, 0.9f}); // a local value shadows the style
    EXPECT_EQ(scene->get_ambient_light(), glm::vec3(0.9f, 0.9f, 0.9f));
    scene->clear_value(Scene::ambient_light_property);
    EXPECT_EQ(scene->get_ambient_light(), glm::vec3(0.1f, 0.2f, 0.3f));

    style->set_ambient_light(glm::vec3{0.4f, 0.4f, 0.4f}); // a live edit of the style follows
    EXPECT_EQ(scene->get_ambient_light(), glm::vec3(0.4f, 0.4f, 0.4f));

    scene->set_style({});
    EXPECT_EQ(scene->get_ambient_light(), glm::vec3(0.0f, 0.0f, 0.0f));
}
