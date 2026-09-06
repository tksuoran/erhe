// Camera projection fields as entry-stored erhe::property properties
// (doc/property-system.md section 4.4): Camera::projection() is a mirror
// of the effective values, every projection property inherits, and a
// node holds them for the cameras below it (D30).

#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_string.hpp"

#include <glm/gtc/constants.hpp>
#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;
using erhe::scene::Camera;
using erhe::scene::Node;
using erhe::scene::Xform;
using erhe::scene::Projection;

TEST(Camera_properties, defaults_match_projection_defaults)
{
    auto camera = std::make_shared<Camera>("c");
    EXPECT_EQ(camera->get_value(Camera::projection_type_property), Projection::Type::perspective_vertical);
    EXPECT_FLOAT_EQ(camera->get_value(Camera::z_near_property), 0.03f);
    EXPECT_FLOAT_EQ(camera->get_value(Camera::z_far_property), 64.0f);
    EXPECT_EQ(camera->get_value_source(Camera::z_far_property), Value_source::default_value);
    const Projection defaults{};
    EXPECT_EQ(camera->projection()->projection_type, defaults.projection_type);
    EXPECT_FLOAT_EQ(camera->projection()->fov_y, defaults.fov_y);
    EXPECT_FLOAT_EQ(camera->projection()->z_far, defaults.z_far);
}

TEST(Camera_properties, property_writes_reach_the_projection_mirror)
{
    auto camera = std::make_shared<Camera>("c");
    camera->set_value(Camera::projection_type_property, Projection::Type::perspective);
    camera->set_value(Camera::fov_x_property, 1.0f);
    camera->set_fov_y(0.5f);
    camera->set_z_near(0.1f);
    camera->set_infinite_z_far(true);
    const Projection* projection = camera->projection();
    EXPECT_EQ(projection->projection_type, Projection::Type::perspective);
    EXPECT_FLOAT_EQ(projection->fov_x, 1.0f);
    EXPECT_FLOAT_EQ(projection->fov_y, 0.5f);
    EXPECT_FLOAT_EQ(projection->z_near, 0.1f);
    EXPECT_TRUE(projection->infinite_z_far);
    EXPECT_EQ(camera->get_value_source(Camera::fov_y_property), Value_source::local);

    camera->clear_value(Camera::fov_x_property);
    EXPECT_FLOAT_EQ(projection->fov_x, Projection{}.fov_x);

    camera->set_projection(Projection{.projection_type = Projection::Type::orthogonal, .z_far = 200.0f, .ortho_width = 12.0f});
    EXPECT_EQ(projection->projection_type, Projection::Type::orthogonal);
    EXPECT_FLOAT_EQ(projection->z_far, 200.0f);
    EXPECT_FLOAT_EQ(projection->ortho_width, 12.0f);
    EXPECT_FLOAT_EQ(projection->fov_y, Projection{}.fov_y);
}

TEST(Camera_properties, untyped_access_with_enumeration_labels)
{
    auto camera = std::make_shared<Camera>("c");
    const Dependency_property* type = Property_registry::get().find_for_object(camera->get_property_owner_type(), "projection_type");
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(to_string(*type, camera->get_value(*type)), "Perspective Vertical");
    camera->set_value(*type, parse_value(*type, "Generic Frustum").value());
    EXPECT_EQ(camera->projection()->projection_type, Projection::Type::generic_frustum);
    EXPECT_FALSE(parse_value(*type, "Fisheye").has_value());

    const Dependency_property* z_far = Property_registry::get().find_for_object(camera->get_property_owner_type(), "z_far");
    ASSERT_NE(z_far, nullptr);
    camera->set_value(*z_far, parse_value(*z_far, "500").value());
    EXPECT_FLOAT_EQ(camera->projection()->z_far, 500.0f);
}

TEST(Camera_properties, node_held_values_are_inherited_into_the_mirror)
{
    auto node   = std::make_shared<Xform>("n");
    auto camera = std::make_shared<Camera>("c");
    erhe::scene::set_prim_parent(camera, node);
    const Property_registry& registry = Property_registry::get();
    EXPECT_TRUE(registry.is_secondary_property(*node, Camera::fov_y_property.get()));
    EXPECT_EQ(registry.find_for_object(*node, "Camera.fov_y"), Camera::fov_y_property.get_ptr());

    node->set_value(Camera::fov_y_property, 0.25f);
    EXPECT_EQ(camera->get_value_source(Camera::fov_y_property), Value_source::inherited);
    EXPECT_FLOAT_EQ(camera->projection()->fov_y, 0.25f);

    camera->set_fov_y(0.75f); // a local value shadows the node
    EXPECT_FLOAT_EQ(camera->projection()->fov_y, 0.75f);
    camera->clear_value(Camera::fov_y_property);
    EXPECT_FLOAT_EQ(camera->projection()->fov_y, 0.25f);
    node->clear_value(Camera::fov_y_property);
    EXPECT_FLOAT_EQ(camera->projection()->fov_y, Projection{}.fov_y);
}

TEST(Camera_properties, exposure_and_shadow_range_live_in_the_store)
{
    auto camera = std::make_shared<Camera>("c");
    EXPECT_FLOAT_EQ(camera->get_exposure(), 1.0f);
    EXPECT_FLOAT_EQ(camera->get_shadow_range(), 22.0f);
    EXPECT_EQ(camera->get_value_source(Camera::exposure_property), Value_source::default_value);
    camera->set_exposure(3.0f);
    camera->set_shadow_range(40.0f);
    EXPECT_EQ(camera->get_value_source(Camera::exposure_property), Value_source::local);
    EXPECT_FLOAT_EQ(camera->get_value(Camera::exposure_property), 3.0f);
    EXPECT_FLOAT_EQ(camera->get_value(Camera::shadow_range_property), 40.0f);
    camera->clear_value(Camera::exposure_property);
    EXPECT_FLOAT_EQ(camera->get_exposure(), 1.0f);
}

TEST(Camera_properties, clone_copies_projection_and_store)
{
    auto camera = std::make_shared<Camera>("c");
    camera->set_z_far(300.0f);
    camera->set_exposure(2.0f);
    const auto clone = std::static_pointer_cast<Camera>(camera->clone());
    ASSERT_TRUE(clone);
    EXPECT_FLOAT_EQ(clone->projection()->z_far, 300.0f);
    EXPECT_FLOAT_EQ(clone->get_value(Camera::z_far_property), 300.0f);
    EXPECT_FLOAT_EQ(clone->get_exposure(), 2.0f);
    EXPECT_EQ(clone->get_value_source(Camera::shadow_range_property), Value_source::default_value);
}
