// A local value is an authored value (doc/property-system.md D32): an
// importer fills a Light or a Camera field by field from a file that
// carries the format's own defaults, and the generic elision pass takes
// back the values that merely repeat the item's default.

#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_property/dependency_object.hpp"

#include <glm/gtc/constants.hpp>
#include <gtest/gtest.h>

#include <string_view>

using namespace erhe::property;
using erhe::scene::Camera;
using erhe::scene::Light;
using erhe::scene::Light_type;
using erhe::scene::Projection;

TEST(Imported_defaults, a_light_written_field_by_field_authors_only_what_differs)
{
    Light light{std::string_view{"imported light"}};
    // What a KHR_lights_punctual import writes: the spec defaults for every
    // field except the type and the intensity.
    light.set_light_type(Light_type::point);
    light.set_color(glm::vec3{1.0f, 1.0f, 1.0f});
    light.set_intensity(40.0f);
    light.set_range(100.0f);
    light.set_inner_spot_angle(glm::pi<float>() * 0.4f);
    light.set_outer_spot_angle(glm::pi<float>() * 0.5f);
    light.set_cast_shadow(true);

    clear_default_valued_local_properties(light);

    EXPECT_EQ(light.get_value_source(Light::light_type_property.get()),       Value_source::local);
    EXPECT_EQ(light.get_value_source(Light::intensity_property.get()),        Value_source::local);
    EXPECT_EQ(light.get_value_source(Light::color_property.get()),            Value_source::default_value);
    EXPECT_EQ(light.get_value_source(Light::range_property.get()),            Value_source::default_value);
    EXPECT_EQ(light.get_value_source(Light::inner_spot_angle_property.get()), Value_source::default_value);
    EXPECT_EQ(light.get_value_source(Light::outer_spot_angle_property.get()), Value_source::default_value);
    EXPECT_EQ(light.get_value_source(Light::cast_shadow_property.get()),      Value_source::default_value);
    EXPECT_EQ(light.get_light_type(), Light_type::point);
    EXPECT_EQ(light.get_intensity(),  40.0f);
}

TEST(Imported_defaults, a_camera_projection_write_authors_only_what_differs)
{
    Camera camera{};
    Projection projection = *camera.projection();
    projection.z_far = 500.0f;
    camera.set_projection(projection);
    camera.set_exposure(1.0f);

    clear_default_valued_local_properties(camera);

    EXPECT_EQ(camera.get_value_source(Camera::z_far_property.get()),    Value_source::local);
    EXPECT_EQ(camera.get_value_source(Camera::z_near_property.get()),   Value_source::default_value);
    EXPECT_EQ(camera.get_value_source(Camera::fov_y_property.get()),    Value_source::default_value);
    EXPECT_EQ(camera.get_value_source(Camera::exposure_property.get()), Value_source::default_value);
    EXPECT_EQ(camera.projection()->z_far, 500.0f);
}
