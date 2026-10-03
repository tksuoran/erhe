// build -> export_gltf (GLB) -> parse_gltf -> compare, without an editor: the
// exporter writes the ERHE_node / ERHE_light / ERHE_camera "properties"
// maps (doc/gltf_extensions/ERHE_node.md) and the importer applies them.

#include "gltf_test_util.hpp"

#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

namespace {

using erhe_gltf_test::data_path;
using erhe_gltf_test::parse_file;
using erhe_gltf_test::write_temporary_file;

template <typename T>
[[nodiscard]] auto find_by_name(const std::vector<std::shared_ptr<T>>& items, const std::string_view name) -> std::shared_ptr<T>
{
    for (const std::shared_ptr<T>& item : items) {
        if (item && (item->get_name() == name)) {
            return item;
        }
    }
    return {};
}

void make_content(erhe::Item_base& item)
{
    item.enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
}

// Exports `scene` as GLB (a .gltf export names an external buffer file the
// string API does not write) and parses the file back into `out_scene`.
[[nodiscard]] auto round_trip(const erhe::scene::Scene& scene, const std::string_view file_name, erhe::scene::Scene& out_scene) -> erhe::gltf::Gltf_data
{
    const std::string text = erhe::gltf::export_gltf(*scene.get_root_node(), true);
    EXPECT_FALSE(text.empty());
    return parse_file(write_temporary_file(file_name, text), out_scene);
}

TEST(Gltf_roundtrip, node_hierarchy_and_transform)
{
    erhe::scene::Scene scene{"source", nullptr};
    const std::shared_ptr<erhe::scene::Xform> group = std::make_shared<erhe::scene::Xform>("Group");
    make_content(*group);
    group->set_parent(scene.get_root_node());
    group->set_parent_from_node(erhe::math::create_translation<float>(glm::vec3{1.0f, 2.0f, 3.0f}));

    const std::shared_ptr<erhe::scene::Xform> child = std::make_shared<erhe::scene::Xform>("Child");
    make_content(*child);
    child->set_parent(group);
    child->set_parent_from_node(erhe::math::create_translation<float>(glm::vec3{0.0f, 0.0f, -4.0f}));

    erhe::scene::Scene          loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = round_trip(scene, "hierarchy.glb", loaded);

    const std::shared_ptr<erhe::scene::Node> loaded_group = find_by_name(data.nodes, "Group");
    const std::shared_ptr<erhe::scene::Node> loaded_child = find_by_name(data.nodes, "Child");
    ASSERT_TRUE(loaded_group);
    ASSERT_TRUE(loaded_child);
    EXPECT_EQ(loaded_child->get_parent_node(), loaded_group);
    const glm::vec3 group_position = glm::vec3{loaded_group->parent_from_node()[3]};
    const glm::vec3 child_position = glm::vec3{loaded_child->parent_from_node()[3]};
    EXPECT_EQ(group_position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(child_position, glm::vec3(0.0f, 0.0f, -4.0f));
}

// A local value without a native glTF carrier rides the ERHE_* "properties"
// map; a native one (KHR_lights_punctual intensity) rides the core field.
// Both come back as local values, and unauthored values stay unauthored.
TEST(Gltf_roundtrip, local_properties)
{
    erhe::scene::Scene scene{"source", nullptr};
    const std::shared_ptr<erhe::scene::Xform> group = std::make_shared<erhe::scene::Xform>("Group");
    make_content(*group);
    group->set_parent(scene.get_root_node());
    group->set_value(erhe::Item_base::visible_property, false);

    const std::shared_ptr<erhe::scene::Light> light = std::make_shared<erhe::scene::Light>("Lamp");
    make_content(*light);
    light->set_parent(group);
    light->set_light_type(erhe::scene::Light_type::point);
    light->set_intensity(42.0f);
    light->set_range(12.0f);
    light->set_temperature(3500.0f);

    const std::shared_ptr<erhe::scene::Camera> camera = std::make_shared<erhe::scene::Camera>("Cam");
    make_content(*camera);
    camera->set_parent(scene.get_root_node());
    camera->set_exposure(2.5f);

    erhe::scene::Scene          loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = round_trip(scene, "properties.glb", loaded);

    const std::shared_ptr<erhe::scene::Node> loaded_group = find_by_name(data.nodes, "Group");
    ASSERT_TRUE(loaded_group);
    EXPECT_FALSE(loaded_group->get_value(erhe::Item_base::visible_property));
    EXPECT_TRUE(loaded_group->has_local_value(erhe::Item_base::visible_property));

    const std::shared_ptr<erhe::scene::Light> loaded_light = find_by_name(data.lights, "Lamp");
    ASSERT_TRUE(loaded_light);
    EXPECT_EQ(loaded_light->get_light_type(), erhe::scene::Light_type::point);
    EXPECT_EQ(loaded_light->get_intensity(),   42.0f);
    EXPECT_EQ(loaded_light->get_range(),       12.0f);
    EXPECT_EQ(loaded_light->get_temperature(), 3500.0f);
    EXPECT_TRUE (loaded_light->has_local_value(erhe::scene::Light::temperature_property));
    EXPECT_FALSE(loaded_light->has_local_value(erhe::scene::Light::inner_spot_angle_property));

    const std::shared_ptr<erhe::scene::Camera> loaded_camera = find_by_name(data.cameras, "Cam");
    ASSERT_TRUE(loaded_camera);
    EXPECT_EQ(loaded_camera->get_exposure(), 2.5f);
    EXPECT_FALSE(loaded_camera->has_local_value(erhe::scene::Camera::shadow_range_property));
}

// The fixture survives parse -> export -> parse: node, mesh primitives with
// their vertex and index counts, and the primitive materials.
TEST(Gltf_roundtrip, variants_fixture_meshes)
{
    erhe::scene::Scene          scene{"source", nullptr};
    const erhe::gltf::Gltf_data source = parse_file(data_path("variants.gltf"), scene);
    ASSERT_EQ(source.nodes.size(), 1u);

    erhe::scene::Scene          loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = round_trip(scene, "variants_roundtrip.glb", loaded);

    const std::shared_ptr<erhe::scene::Node> panel = find_by_name(data.nodes, "Panel");
    ASSERT_TRUE(panel);
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(panel.get());
    ASSERT_TRUE(mesh);
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    ASSERT_EQ(primitives.size(), 2u);
    for (const erhe::scene::Mesh_primitive& mesh_primitive : primitives) {
        ASSERT_TRUE(mesh_primitive.material);
        EXPECT_EQ(mesh_primitive.material->get_name(), "Plain");
        ASSERT_TRUE(mesh_primitive.primitive);
        const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape =
            mesh_primitive.primitive->get_render_shape(erhe::primitive::Mesh_variant::original);
        ASSERT_TRUE(shape);
        const std::shared_ptr<erhe::primitive::Triangle_soup>& soup = shape->get_triangle_soup();
        ASSERT_TRUE(soup);
        EXPECT_EQ(soup->get_vertex_count(), 4u);
        EXPECT_EQ(soup->get_index_count(), 6u);
    }
}

} // anonymous namespace
