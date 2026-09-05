#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

class Cube_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Node>("import_root");
        const erhe::usd::Usd_load_arguments arguments{
            .path          = test_data_path("cube.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        result = erhe::usd::load_usd(arguments);
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Cube_import, load_succeeds)
{
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_FALSE(result.data.nodes.empty());
    EXPECT_EQ(result.data.up_axis, "Y");
    EXPECT_DOUBLE_EQ(result.data.meters_per_unit, 1.0);
}

TEST_F(Cube_import, node_names)
{
    bool cube_found   = false;
    bool camera_found = false;
    bool light_found  = false;
    for (const std::shared_ptr<erhe::scene::Node>& node : result.data.nodes) {
        ASSERT_TRUE(node.operator bool());
        if (node->get_name() == "cube") {
            cube_found = true;
        }
        if (node->get_name() == "cam") {
            camera_found = true;
        }
        if (node->get_name() == "sun") {
            light_found = true;
        }
    }
    EXPECT_TRUE(cube_found);
    EXPECT_TRUE(camera_found);
    EXPECT_TRUE(light_found);
}

TEST_F(Cube_import, non_scene_prims_contribute_no_nodes)
{
    // The Scope holding the materials, the Material prims, their Shader prims
    // and the GeomSubset are namespace, not scene graph: none of them becomes
    // a node. What is left is root + cube + cam + sun.
    std::string names;
    for (const std::shared_ptr<erhe::scene::Node>& node : result.data.nodes) {
        ASSERT_TRUE(node.operator bool());
        const std::string& name = node->get_name();
        EXPECT_NE(name, "materials");
        EXPECT_NE(name, "Red");
        EXPECT_NE(name, "Blue");
        EXPECT_NE(name, "surface");
        EXPECT_NE(name, "top");
        names += name + " ";
    }
    EXPECT_EQ(result.data.nodes.size(), 4u) << names;
}

TEST_F(Cube_import, mesh_is_geometry_normative_and_split_by_subset)
{
    ASSERT_EQ(result.data.meshes.size(), 1u);
    const erhe::scene::Mesh& mesh = *result.data.meshes.front().get();
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh.get_primitives();
    // One primitive for the materialBind GeomSubset (one face), one for the
    // five faces the subset does not claim.
    ASSERT_EQ(primitives.size(), 2u);

    std::size_t total_facets  = 0;
    std::size_t total_corners = 0;
    for (const erhe::scene::Mesh_primitive& mesh_primitive : primitives) {
        ASSERT_TRUE(mesh_primitive.primitive.operator bool());
        ASSERT_TRUE(mesh_primitive.primitive->render_shape.operator bool());
        const std::shared_ptr<erhe::geometry::Geometry>& geometry =
            mesh_primitive.primitive->render_shape->get_geometry_const();
        ASSERT_TRUE(geometry.operator bool()) << "subdivisionScheme = none must import as erhe geometry";
        total_facets  += geometry->get_mesh().facets.nb();
        total_corners += geometry->get_mesh().facet_corners.nb();
    }
    EXPECT_EQ(total_facets,  6u);
    EXPECT_EQ(total_corners, 24u);

    // The subset primitive holds exactly the one face the subset names.
    bool one_facet_primitive_found = false;
    for (const erhe::scene::Mesh_primitive& mesh_primitive : primitives) {
        const std::shared_ptr<erhe::geometry::Geometry>& geometry =
            mesh_primitive.primitive->render_shape->get_geometry_const();
        if (geometry->get_mesh().facets.nb() == 1) {
            one_facet_primitive_found = true;
            ASSERT_TRUE(mesh_primitive.material.operator bool());
            EXPECT_EQ(mesh_primitive.material->get_name(), "Blue");
        }
    }
    EXPECT_TRUE(one_facet_primitive_found);
}

TEST_F(Cube_import, materials)
{
    ASSERT_EQ(result.data.materials.size(), 2u);
    std::shared_ptr<erhe::primitive::Material> red;
    for (const std::shared_ptr<erhe::primitive::Material>& material : result.data.materials) {
        if (material->get_name() == "Red") {
            red = material;
        }
    }
    ASSERT_TRUE(red.operator bool());
    const erhe::primitive::Material_values values = red->get_values();
    EXPECT_NEAR(values.base_color.x, 0.8f, 1e-5f);
    EXPECT_NEAR(values.base_color.y, 0.1f, 1e-5f);
    EXPECT_NEAR(values.base_color.z, 0.1f, 1e-5f);
    EXPECT_NEAR(values.roughness.x,  0.4f, 1e-5f);
    EXPECT_NEAR(values.metallic,     0.0f, 1e-5f);
}

TEST_F(Cube_import, camera)
{
    ASSERT_EQ(result.data.cameras.size(), 1u);
    const erhe::scene::Camera& camera = *result.data.cameras.front().get();
    const erhe::scene::Projection* projection = camera.projection();
    ASSERT_NE(projection, nullptr);
    EXPECT_EQ(projection->projection_type, erhe::scene::Projection::Type::perspective_vertical);
    // verticalAperture 24 mm at focal length 50 mm.
    const float expected_fov_y = 2.0f * std::atan(0.5f * 24.0f / 50.0f);
    EXPECT_NEAR(projection->fov_y,  expected_fov_y, 1e-4f);
    EXPECT_NEAR(projection->z_near, 0.1f,           1e-4f);
    EXPECT_NEAR(projection->z_far,  1000.0f,        1e-1f);
}

TEST_F(Cube_import, light)
{
    ASSERT_EQ(result.data.lights.size(), 1u);
    ASSERT_TRUE(result.data.lights.front().operator bool());
    const erhe::scene::Light& light = *result.data.lights.front().get();
    EXPECT_EQ(light.get_light_type(), erhe::scene::Light_type::directional);
    EXPECT_NEAR(light.get_intensity(), 3.0f, 1e-5f);
    EXPECT_NEAR(light.get_color().x,   1.0f, 1e-5f);
    EXPECT_NEAR(light.get_color().y,   0.9f, 1e-5f);
    EXPECT_NEAR(light.get_color().z,   0.8f, 1e-5f);
}

// doc/usd-compatibility-plan.md I2: an attribute the composed stage has an
// authored opinion for becomes a local value; an attribute at its schema
// fallback leaves the erhe property at the ERHE default.
class Authored_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Node>("import_root");
        const erhe::usd::Usd_load_arguments arguments{
            .path          = test_data_path("authored.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        result = erhe::usd::load_usd(arguments);
    }

    [[nodiscard]] auto find_node(const std::string& name) const -> std::shared_ptr<erhe::scene::Node>
    {
        for (const std::shared_ptr<erhe::scene::Node>& node : result.data.nodes) {
            if (node && (node->get_name() == name)) {
                return node;
            }
        }
        return {};
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         result;
};

TEST_F(Authored_import, load_succeeds)
{
    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_FALSE(result.data.nodes.empty());
}

TEST_F(Authored_import, authored_visibility_is_local)
{
    const std::shared_ptr<erhe::scene::Node> hidden = find_node("hidden");
    ASSERT_TRUE(hidden.operator bool());
    EXPECT_EQ(hidden->get_value_source(erhe::Item_base::visible_property.get()), erhe::property::Value_source::local);
    EXPECT_FALSE(hidden->get_value(erhe::Item_base::visible_property));
}

TEST_F(Authored_import, unauthored_visibility_stays_default)
{
    const std::shared_ptr<erhe::scene::Node> shown = find_node("shown");
    ASSERT_TRUE(shown.operator bool());
    EXPECT_EQ(shown->get_value_source(erhe::Item_base::visible_property.get()), erhe::property::Value_source::default_value);
    EXPECT_TRUE(shown->get_value(erhe::Item_base::visible_property));
}

TEST_F(Authored_import, authored_purpose_is_local)
{
    const std::shared_ptr<erhe::scene::Node> helper = find_node("helper");
    ASSERT_TRUE(helper.operator bool());
    EXPECT_EQ(helper->get_value_source(erhe::Item_base::purpose_property.get()), erhe::property::Value_source::local);
    EXPECT_EQ(helper->get_value(erhe::Item_base::purpose_property), erhe::Purpose::guide);

    const std::shared_ptr<erhe::scene::Node> shown = find_node("shown");
    ASSERT_TRUE(shown.operator bool());
    EXPECT_EQ(shown->get_value_source(erhe::Item_base::purpose_property.get()), erhe::property::Value_source::default_value);
}

TEST_F(Authored_import, only_authored_shader_inputs_are_local)
{
    ASSERT_EQ(result.data.materials.size(), 1u);
    const std::shared_ptr<erhe::primitive::Material>& material = result.data.materials.front();
    ASSERT_TRUE(material.operator bool());
    using erhe::primitive::Material;
    EXPECT_EQ(material->get_value_source(Material::base_color_property.get()), erhe::property::Value_source::local);
    const glm::vec3 base_color = material->get_value(Material::base_color_property);
    EXPECT_NEAR(base_color.x, 0.2f, 1e-5f);
    EXPECT_NEAR(base_color.y, 0.4f, 1e-5f);
    EXPECT_NEAR(base_color.z, 0.6f, 1e-5f);
    // The USD fallbacks of the inputs the shader leaves alone never reach
    // the material: each property stays at the ERHE default.
    EXPECT_EQ(material->get_value_source(Material::metallic_property.get()),      erhe::property::Value_source::default_value);
    EXPECT_EQ(material->get_value_source(Material::roughness_property.get()),     erhe::property::Value_source::default_value);
    EXPECT_EQ(material->get_value_source(Material::opacity_property.get()),       erhe::property::Value_source::default_value);
    EXPECT_EQ(material->get_value_source(Material::ior_property.get()),           erhe::property::Value_source::default_value);
    EXPECT_EQ(material->get_value_source(Material::emissive_property.get()),      erhe::property::Value_source::default_value);
    EXPECT_EQ(material->get_value_source(Material::blending_mode_property.get()), erhe::property::Value_source::default_value);
}

TEST_F(Authored_import, erhe_custom_attributes_become_property_values)
{
    ASSERT_EQ(result.data.lights.size(), 1u);
    const std::shared_ptr<erhe::scene::Light>& light = result.data.lights.front();
    ASSERT_TRUE(light.operator bool());
    using erhe::scene::Light;
    // `custom float erhe:Light:temperature = 5000` on the prim.
    EXPECT_EQ(light->get_value_source(Light::temperature_property.get()), erhe::property::Value_source::local);
    EXPECT_NEAR(light->get_value(Light::temperature_property), 5000.0f, 1e-3f);
    // The authored UsdLux input.
    EXPECT_EQ(light->get_value_source(Light::color_property.get()), erhe::property::Value_source::local);
    // `erhe:Light:nope` names no property: one warning, no value.
    // `inputs:intensity` is not authored.
    EXPECT_EQ(light->get_value_source(Light::intensity_property.get()), erhe::property::Value_source::default_value);
}

TEST(Usd_import, missing_file_is_an_error)
{
    const std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Node>("import_root");
    const erhe::usd::Usd_load_arguments arguments{
        .path          = test_data_path("this_file_does_not_exist.usda"),
        .root_node     = root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result result = erhe::usd::load_usd(arguments);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.data.nodes.empty());
}

} // anonymous namespace
