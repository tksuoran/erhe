#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_export_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto find_node(const erhe::usd::Usd_data& data, const std::string& name) -> std::shared_ptr<erhe::scene::Node>
{
    for (const std::shared_ptr<erhe::scene::Node>& node : data.nodes) {
        if (node && (node->get_name() == name)) {
            return node;
        }
    }
    return {};
}

[[nodiscard]] auto find_material(const erhe::usd::Usd_data& data, const std::string& name) -> std::shared_ptr<erhe::primitive::Material>
{
    for (const std::shared_ptr<erhe::primitive::Material>& material : data.materials) {
        if (material && (material->get_name() == name)) {
            return material;
        }
    }
    return {};
}

// Load a USD file, write it back out with save_usda, and load the result.
// Everything the writer is asked to preserve has to survive that pair.
class Round_trip
{
public:
    explicit Round_trip(const char* source_file_name)
    {
        source_root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path(source_file_name),
            .root_node     = source_root,
            .mesh_layer_id = 0
        };
        source = erhe::usd::load_usd(load_arguments);
    }

    void save_and_reload(const char* written_file_name)
    {
        written_path = temporary_path(written_file_name);
        const erhe::usd::Usd_save_arguments save_arguments{
            .path      = written_path,
            .root_node = source_root,
            .materials = source.data.materials
        };
        save = erhe::usd::save_usda(save_arguments);

        reloaded_root = std::make_shared<erhe::scene::Xform>("reload_root");
        const erhe::usd::Usd_load_arguments reload_arguments{
            .path          = written_path,
            .root_node     = reloaded_root,
            .mesh_layer_id = 0
        };
        reloaded = erhe::usd::load_usd(reload_arguments);
    }

    std::shared_ptr<erhe::scene::Node> source_root;
    std::shared_ptr<erhe::scene::Node> reloaded_root;
    std::filesystem::path              written_path;
    erhe::usd::Usd_load_result         source;
    erhe::usd::Usd_save_result         save;
    erhe::usd::Usd_load_result         reloaded;
};

class Cube_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        trip = std::make_unique<Round_trip>("cube.usda");
        ASSERT_TRUE(trip->source.error.empty()) << trip->source.error;
        trip->save_and_reload("cube.usda");
        ASSERT_TRUE(trip->save.error.empty()) << trip->save.error;
        ASSERT_TRUE(trip->reloaded.error.empty()) << trip->reloaded.error;
    }

    std::unique_ptr<Round_trip> trip;
};

TEST_F(Cube_round_trip, file_is_written)
{
    EXPECT_TRUE(std::filesystem::exists(trip->written_path));
    EXPECT_EQ(trip->reloaded.data.up_axis, "Y");
    EXPECT_DOUBLE_EQ(trip->reloaded.data.meters_per_unit, 1.0);
}

TEST_F(Cube_round_trip, node_names_survive)
{
    EXPECT_TRUE(find_node(trip->reloaded.data, "root").operator bool());
    EXPECT_TRUE(find_node(trip->reloaded.data, "cube").operator bool());
    EXPECT_TRUE(find_node(trip->reloaded.data, "cam").operator bool());
    EXPECT_TRUE(find_node(trip->reloaded.data, "sun").operator bool());
    // The Materials scope and its shading network are namespace, not scene
    // graph: the four prims above are the whole node list.
    EXPECT_EQ(trip->reloaded.data.nodes.size(), 4u);
}

TEST_F(Cube_round_trip, mesh_topology_survives)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(trip->reloaded.data, "cube");
    ASSERT_TRUE(node.operator bool());
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
    ASSERT_TRUE(mesh.operator bool());

    // One primitive per materialBind GeomSubset, the way the source file's
    // subset plus its remainder came in.
    ASSERT_EQ(mesh->get_primitives().size(), 2u);
    std::size_t facet_count  = 0;
    std::size_t vertex_count = 0;
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
        ASSERT_TRUE(mesh_primitive.primitive.operator bool());
        ASSERT_TRUE(mesh_primitive.primitive->render_shape.operator bool());
        const std::shared_ptr<erhe::geometry::Geometry>& geometry = mesh_primitive.primitive->render_shape->get_geometry_const();
        ASSERT_TRUE(geometry.operator bool());
        facet_count  += geometry->get_mesh().facets.nb();
        vertex_count += geometry->get_mesh().vertices.nb();
    }
    EXPECT_EQ(facet_count, 6u);
    // Four vertices for the one-facet subset, eight for the five-facet rest.
    EXPECT_EQ(vertex_count, 12u);
}

TEST_F(Cube_round_trip, subset_material_bindings_survive)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(trip->reloaded.data, "cube");
    ASSERT_TRUE(node.operator bool());
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
    ASSERT_TRUE(mesh.operator bool());

    std::set<std::string> bound_material_names;
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
        ASSERT_TRUE(mesh_primitive.material.operator bool());
        bound_material_names.insert(mesh_primitive.material->get_name());
    }
    EXPECT_EQ(bound_material_names, (std::set<std::string>{"Blue", "Red"}));
}

TEST_F(Cube_round_trip, material_local_sets_survive)
{
    using erhe::primitive::Material;
    const std::shared_ptr<Material> red = find_material(trip->reloaded.data, "Red");
    ASSERT_TRUE(red.operator bool());
    EXPECT_EQ(red->get_value_source(Material::base_color_property.get()), erhe::property::Value_source::local);
    EXPECT_NEAR(red->get_value(Material::base_color_property).x, 0.8f, 1e-5f);
    EXPECT_NEAR(red->get_value(Material::base_color_property).y, 0.1f, 1e-5f);
    EXPECT_NEAR(red->get_value(Material::base_color_property).z, 0.1f, 1e-5f);
    EXPECT_NEAR(red->get_value(Material::roughness_property).x, 0.4f, 1e-5f);

    const std::shared_ptr<Material> blue = find_material(trip->reloaded.data, "Blue");
    ASSERT_TRUE(blue.operator bool());
    EXPECT_NEAR(blue->get_value(Material::metallic_property), 1.0f, 1e-5f);
    EXPECT_NEAR(blue->get_value(Material::base_color_property).z, 0.9f, 1e-5f);
    // Nothing the source file left unauthored became a local value.
    EXPECT_NE(blue->get_value_source(Material::ior_property.get()), erhe::property::Value_source::local);
}

// A `Camera` prim and a UsdLux prim of the stage are erhe Camera / Light
// prims with their own xformOps, not Xforms carrying an attachment
// (doc/usd-compatibility-plan.md C5).
TEST_F(Cube_round_trip, camera_and_light_prims_round_trip_with_their_own_xform_ops)
{
    const std::shared_ptr<erhe::scene::Node> cam = find_node(trip->reloaded.data, "cam");
    ASSERT_TRUE(cam.operator bool());
    EXPECT_TRUE(erhe::is<erhe::scene::Camera>(cam.get()));
    EXPECT_EQ(cam->get_class_type_name(), "Camera");
    EXPECT_TRUE(cam->get_attachments().empty());
    const glm::vec3 cam_translation = cam->parent_from_node_transform().get_translation();
    EXPECT_NEAR(cam_translation.x, 0.0f, 1e-5f);
    EXPECT_NEAR(cam_translation.y, 1.0f, 1e-5f);
    EXPECT_NEAR(cam_translation.z, 7.0f, 1e-5f);

    const std::shared_ptr<erhe::scene::Node> sun = find_node(trip->reloaded.data, "sun");
    ASSERT_TRUE(sun.operator bool());
    EXPECT_TRUE(erhe::is<erhe::scene::Light>(sun.get()));
    EXPECT_EQ(sun->get_class_type_name(), "Light");
    EXPECT_TRUE(sun->get_attachments().empty());
    const glm::vec3 sun_translation = sun->parent_from_node_transform().get_translation();
    EXPECT_NEAR(sun_translation.x, 3.0f, 1e-5f);
    EXPECT_NEAR(sun_translation.y, 4.0f, 1e-5f);
    EXPECT_NEAR(sun_translation.z, 5.0f, 1e-5f);
}

TEST_F(Cube_round_trip, camera_values_survive)
{
    using erhe::scene::Camera;
    const std::shared_ptr<erhe::scene::Node> node = find_node(trip->reloaded.data, "cam");
    ASSERT_TRUE(node.operator bool());
    const std::shared_ptr<Camera> camera = erhe::scene::get_camera(node.get());
    ASSERT_TRUE(camera.operator bool());

    EXPECT_NEAR(camera->get_value(Camera::z_near_property), 0.1f, 1e-4f);
    EXPECT_NEAR(camera->get_value(Camera::z_far_property), 1000.0f, 1e-2f);
    EXPECT_EQ(camera->get_value(Camera::projection_type_property), erhe::scene::Projection::Type::perspective_vertical);
    // The source file's 50 mm lens with a 36 x 24 aperture.
    EXPECT_NEAR(camera->get_value(Camera::fov_y_property), 2.0f * std::atan(12.0f / 50.0f), 1e-4f);
    EXPECT_NEAR(camera->get_value(Camera::fov_x_property), 2.0f * std::atan(18.0f / 50.0f), 1e-4f);
}

TEST_F(Cube_round_trip, light_values_survive)
{
    using erhe::scene::Light;
    const std::shared_ptr<erhe::scene::Node> node = find_node(trip->reloaded.data, "sun");
    ASSERT_TRUE(node.operator bool());
    const std::shared_ptr<Light> light = erhe::scene::get_light(node.get());
    ASSERT_TRUE(light.operator bool());

    EXPECT_EQ(light->get_value(Light::light_type_property), erhe::scene::Light_type::directional);
    EXPECT_NEAR(light->get_value(Light::color_property).x, 1.0f, 1e-5f);
    EXPECT_NEAR(light->get_value(Light::color_property).y, 0.9f, 1e-5f);
    EXPECT_NEAR(light->get_value(Light::color_property).z, 0.8f, 1e-5f);
    EXPECT_NEAR(light->get_value(Light::intensity_property), 3.0f, 1e-5f);
}

class Authored_round_trip : public testing::Test
{
protected:
    void SetUp() override
    {
        trip = std::make_unique<Round_trip>("authored.usda");
        ASSERT_TRUE(trip->source.error.empty()) << trip->source.error;

        // An erhe-only property with no USD attribute of its own: it can only
        // come back through the `erhe:Owner:name` custom attribute form.
        const std::shared_ptr<erhe::scene::Node> lamp_node = find_node(trip->source.data, "lamp");
        ASSERT_TRUE(lamp_node.operator bool());
        const std::shared_ptr<erhe::scene::Light> lamp = erhe::scene::get_light(lamp_node.get());
        ASSERT_TRUE(lamp.operator bool());
        lamp->set_value(erhe::scene::Light::range_property, 12.5f);

        trip->save_and_reload("authored.usda");
        ASSERT_TRUE(trip->save.error.empty()) << trip->save.error;
        ASSERT_TRUE(trip->reloaded.error.empty()) << trip->reloaded.error;
    }

    std::unique_ptr<Round_trip> trip;
};

TEST_F(Authored_round_trip, visibility_survives)
{
    const std::shared_ptr<erhe::scene::Node> hidden = find_node(trip->reloaded.data, "hidden");
    ASSERT_TRUE(hidden.operator bool());
    EXPECT_EQ(hidden->get_value_source(erhe::Item_base::visible_property.get()), erhe::property::Value_source::local);
    EXPECT_FALSE(hidden->get_value(erhe::Item_base::visible_property));

    const std::shared_ptr<erhe::scene::Node> shown = find_node(trip->reloaded.data, "shown");
    ASSERT_TRUE(shown.operator bool());
    EXPECT_TRUE(shown->get_value(erhe::Item_base::visible_property));
}

TEST_F(Authored_round_trip, purpose_survives)
{
    const std::shared_ptr<erhe::scene::Node> helper = find_node(trip->reloaded.data, "helper");
    ASSERT_TRUE(helper.operator bool());
    EXPECT_EQ(helper->get_value_source(erhe::Item_base::purpose_property.get()), erhe::property::Value_source::local);
    EXPECT_EQ(helper->get_value(erhe::Item_base::purpose_property), erhe::Purpose::guide);
}

TEST_F(Authored_round_trip, light_temperature_survives)
{
    using erhe::scene::Light;
    const std::shared_ptr<erhe::scene::Node> node = find_node(trip->reloaded.data, "lamp");
    ASSERT_TRUE(node.operator bool());
    const std::shared_ptr<Light> light = erhe::scene::get_light(node.get());
    ASSERT_TRUE(light.operator bool());

    // The source file authors the temperature as `erhe:Light:temperature`;
    // the writer carries it in the UsdLux attribute pair the mapping names
    // (`inputs:colorTemperature` + `inputs:enableColorTemperature`), which is
    // what the importer reads back.
    EXPECT_EQ(light->get_value_source(Light::temperature_property.get()), erhe::property::Value_source::local);
    EXPECT_NEAR(light->get_value(Light::temperature_property), 5000.0f, 1e-3f);
}

TEST_F(Authored_round_trip, erhe_only_property_survives_as_custom_attribute)
{
    using erhe::scene::Light;
    const std::shared_ptr<erhe::scene::Node> node = find_node(trip->reloaded.data, "lamp");
    ASSERT_TRUE(node.operator bool());
    const std::shared_ptr<Light> light = erhe::scene::get_light(node.get());
    ASSERT_TRUE(light.operator bool());

    EXPECT_EQ(light->get_value_source(Light::range_property.get()), erhe::property::Value_source::local);
    EXPECT_NEAR(light->get_value(Light::range_property), 12.5f, 1e-4f);
}

TEST_F(Authored_round_trip, mesh_shadow_cast_survives_as_custom_attribute)
{
    const std::shared_ptr<erhe::scene::Node> node = find_node(trip->reloaded.data, "shown");
    ASSERT_TRUE(node.operator bool());
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
    ASSERT_TRUE(mesh.operator bool());
    EXPECT_TRUE(mesh->get_value(erhe::scene::Mesh::shadow_cast_property));
}

TEST_F(Authored_round_trip, unauthored_material_inputs_stay_unauthored)
{
    using erhe::primitive::Material;
    const std::shared_ptr<Material> material = find_material(trip->reloaded.data, "OnlyDiffuse");
    ASSERT_TRUE(material.operator bool());
    EXPECT_EQ(material->get_value_source(Material::base_color_property.get()), erhe::property::Value_source::local);
    EXPECT_NEAR(material->get_value(Material::base_color_property).x, 0.2f, 1e-5f);
    EXPECT_NEAR(material->get_value(Material::base_color_property).y, 0.4f, 1e-5f);
    EXPECT_NEAR(material->get_value(Material::base_color_property).z, 0.6f, 1e-5f);
    EXPECT_NE(material->get_value_source(Material::metallic_property.get()), erhe::property::Value_source::local);
    EXPECT_NE(material->get_value_source(Material::roughness_property.get()), erhe::property::Value_source::local);
}

TEST(Usd_identifier, sanitizing)
{
    EXPECT_EQ(erhe::usd::sanitize_usd_identifier("Cube.001"), "Cube_001");
    EXPECT_EQ(erhe::usd::sanitize_usd_identifier("1st"), "_1st");
    EXPECT_EQ(erhe::usd::sanitize_usd_identifier("a b-c"), "a_b_c");
    EXPECT_EQ(erhe::usd::sanitize_usd_identifier(""), "_");
    EXPECT_EQ(erhe::usd::sanitize_usd_identifier("already_valid_9"), "already_valid_9");
}

// Two erhe names that sanitize onto one spelling still yield two prims: the
// writer's name scope applies the M2 suffix rule after sanitizing.
TEST(Usd_identifier, colliding_names_are_suffixed)
{
    const std::shared_ptr<erhe::scene::Node> root = std::make_shared<erhe::scene::Xform>("root");
    const std::shared_ptr<erhe::scene::Node> a    = std::make_shared<erhe::scene::Xform>("a.b");
    const std::shared_ptr<erhe::scene::Node> b    = std::make_shared<erhe::scene::Xform>("a b");
    a->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    b->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    a->Hierarchy::set_parent(root);
    b->Hierarchy::set_parent(root);

    const std::filesystem::path        path = temporary_path("collision.usda");
    const erhe::usd::Usd_save_arguments save_arguments{.path = path, .root_node = root};
    const erhe::usd::Usd_save_result    save = erhe::usd::save_usda(save_arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;

    const std::shared_ptr<erhe::scene::Node> reload_root = std::make_shared<erhe::scene::Xform>("reload_root");
    const erhe::usd::Usd_load_arguments      load_arguments{.path = path, .root_node = reload_root, .mesh_layer_id = 0};
    const erhe::usd::Usd_load_result         loaded = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(loaded.error.empty()) << loaded.error;

    // Both prims live under the World wrapper the writer adds when a scene
    // has more than one top-level prim.
    EXPECT_TRUE(find_node(loaded.data, "a_b").operator bool());
    EXPECT_TRUE(find_node(loaded.data, "a_b_1").operator bool());
    EXPECT_TRUE(find_node(loaded.data, "World").operator bool());
}

} // anonymous namespace
