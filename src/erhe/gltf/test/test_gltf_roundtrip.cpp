// build -> export_gltf (GLB) -> parse_gltf -> compare, without an editor: the
// exporter writes the ERHE_node / ERHE_light / ERHE_camera "properties"
// maps (doc/gltf_extensions/ERHE_node.md) and the importer applies them.

#include "gltf_test_util.hpp"

#include "erhe_item/item.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/mesh_optimizer.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_raytrace/prebuilt_bvh.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <simdjson.h>
#include <taskflow/taskflow.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <string>
#include <vector>

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

// A node's TRS components survive save -> load -> save bit for bit: the
// exporter writes the components the node holds, not a glm::decompose() of
// the composed matrix (which differs by a few ulps and drifted every save).
TEST(Gltf_roundtrip, node_trs_bit_exact)
{
    const glm::vec3 translation{1.1f, -2.3f, 3.7f};
    const glm::quat rotation = glm::normalize(glm::quat{0.4f, 0.3f, 0.5f, -0.7f});
    const glm::vec3 scale      {1.5f, 0.25f, 2.0f};

    erhe::scene::Scene scene{"source", nullptr};
    const std::shared_ptr<erhe::scene::Xform> node = std::make_shared<erhe::scene::Xform>("Node");
    make_content(*node);
    node->set_parent(scene.get_root_node());
    node->node_data.transforms.parent_from_node.set_trs(translation, rotation, scale);
    node->update_world_from_node();

    // A child under a scaled, translated parent: the parse must keep the
    // child's TRS as read, not re-derive it from the world transform.
    const glm::vec3 child_translation{0.0f, 0.027f, 0.006f};
    const glm::quat child_rotation = glm::normalize(glm::quat{0.7071066f, -0.7071069f, 0.0f, 0.0f});
    const glm::vec3 child_scale      {0.016f, 0.016f, 0.016f};
    const std::shared_ptr<erhe::scene::Xform> child = std::make_shared<erhe::scene::Xform>("Child");
    make_content(*child);
    child->set_parent(node);
    child->node_data.transforms.parent_from_node.set_trs(child_translation, child_rotation, child_scale);
    child->update_world_from_node();

    const auto expect_node_trs = [&](
        const erhe::gltf::Gltf_data& data,
        const std::string_view       name,
        const glm::vec3              expected_translation,
        const glm::quat              expected_rotation,
        const glm::vec3              expected_scale
    ) {
        const std::shared_ptr<erhe::scene::Node> loaded_node = find_by_name(data.nodes, name);
        ASSERT_TRUE(loaded_node);
        const erhe::scene::Trs_transform& loaded = loaded_node->node_data.transforms.parent_from_node;
        const glm::vec3 loaded_translation = loaded.get_translation();
        const glm::quat loaded_rotation    = loaded.get_rotation();
        const glm::vec3 loaded_scale       = loaded.get_scale();
        EXPECT_EQ(0, std::memcmp(&loaded_translation, &expected_translation, sizeof(glm::vec3))) << name;
        EXPECT_EQ(0, std::memcmp(&loaded_rotation,    &expected_rotation,    sizeof(glm::quat))) << name;
        EXPECT_EQ(0, std::memcmp(&loaded_scale,       &expected_scale,       sizeof(glm::vec3))) << name;
    };
    const auto expect_trs = [&](const erhe::gltf::Gltf_data& data) {
        expect_node_trs(data, "Node",  translation,       rotation,       scale);
        expect_node_trs(data, "Child", child_translation, child_rotation, child_scale);
    };

    erhe::scene::Scene          loaded_1{"loaded_1", nullptr};
    const erhe::gltf::Gltf_data data_1 = round_trip(scene, "trs_1.glb", loaded_1);
    expect_trs(data_1);

    erhe::scene::Scene          loaded_2{"loaded_2", nullptr};
    const erhe::gltf::Gltf_data data_2 = round_trip(loaded_1, "trs_2.glb", loaded_2);
    expect_trs(data_2);
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

// The JSON chunk of a GLB.
[[nodiscard]] auto glb_json(const std::string& glb) -> std::string
{
    EXPECT_GE(glb.size(), 20u);
    uint32_t json_length = 0;
    std::memcpy(&json_length, glb.data() + 12, sizeof(uint32_t));
    EXPECT_LE(20u + json_length, glb.size());
    return glb.substr(20, json_length);
}

[[nodiscard]] auto contains(simdjson::dom::array array, const std::string_view value) -> bool
{
    for (const simdjson::dom::element element : array) {
        if (std::string_view{element.get_string().value()} == value) {
            return true;
        }
    }
    return false;
}

// Prepared mesh data (doc/plans/gltf_fast_load.md step 4): soup primitives
// are written as prepared cores with ERHE_mesh_optimized, ERHE_mesh_topology
// and ERHE_raytrace_bvh, the names are in extensionsUsed only, the payload
// bufferViews have the sizes the specs state, and the file still loads.
TEST(Gltf_roundtrip, prepared_mesh_data_extensions)
{
    erhe::scene::Scene          scene{"source", nullptr};
    const erhe::gltf::Gltf_data source = parse_file(data_path("variants.gltf"), scene);
    ASSERT_EQ(source.nodes.size(), 1u);

    erhe::gltf::Gltf_export_prepared_statistics statistics;
    const erhe::gltf::Gltf_export_arguments arguments{
        .root_node           = *scene.get_root_node(),
        .binary              = true,
        .prepared_mesh_data  = erhe::gltf::Prepared_mesh_data::write,
        .prepared_statistics = &statistics
    };
    const std::string glb = erhe::gltf::export_gltf(arguments);
    ASSERT_FALSE(glb.empty());
    EXPECT_EQ(statistics.prepared_count, 2u);
    EXPECT_EQ(statistics.skipped_count, 0u);
    const bool has_bvh_backend = !erhe::raytrace::get_prebuilt_bvh_backend().empty();
    EXPECT_EQ(statistics.bvh_rebuilt_count, has_bvh_backend ? 2u : 0u);

    const std::string           json_text = glb_json(glb);
    simdjson::dom::parser       parser;
    const simdjson::dom::element root = parser.parse(json_text);

    const simdjson::dom::array extensions_used = root["extensionsUsed"].get_array();
    EXPECT_TRUE(contains(extensions_used, "ERHE_mesh_optimized"));
    EXPECT_TRUE(contains(extensions_used, "ERHE_mesh_topology"));
    EXPECT_EQ(contains(extensions_used, "ERHE_raytrace_bvh"), has_bvh_backend);
    simdjson::dom::array extensions_required;
    if (root["extensionsRequired"].get(extensions_required) == simdjson::SUCCESS) {
        EXPECT_FALSE(contains(extensions_required, "ERHE_mesh_optimized"));
        EXPECT_FALSE(contains(extensions_required, "ERHE_mesh_topology"));
        EXPECT_FALSE(contains(extensions_required, "ERHE_raytrace_bvh"));
    }

    const simdjson::dom::array accessors    = root["accessors"].get_array();
    const simdjson::dom::array buffer_views = root["bufferViews"].get_array();
    const auto check_raw_view = [&](const uint64_t view_index, const uint64_t expected_length, const char* const what) {
        const simdjson::dom::element view = buffer_views.at(view_index);
        EXPECT_EQ(view["byteLength"].get_uint64().value(), expected_length) << what;
        EXPECT_EQ(view["byteStride"].error(), simdjson::NO_SUCH_FIELD) << what;
        EXPECT_EQ(view["target"].error(),     simdjson::NO_SUCH_FIELD) << what;
    };
    std::size_t prepared_primitive_count = 0;
    for (const simdjson::dom::element mesh : root["meshes"].get_array()) {
        for (const simdjson::dom::element primitive : mesh["primitives"].get_array()) {
            const simdjson::dom::element extensions = primitive["extensions"];
            const simdjson::dom::element optimized  = extensions["ERHE_mesh_optimized"];
            ASSERT_FALSE(optimized.is_null());
            ++prepared_primitive_count;
            EXPECT_EQ(optimized["version"].get_uint64().value(), 1u);
            EXPECT_EQ(std::string_view{optimized["content_hash"].get_string().value()}.size(), 16u);

            const uint64_t index_count    = accessors.at(primitive["indices"].get_uint64().value())["count"].get_uint64().value();
            const uint64_t position_count = accessors.at(primitive["attributes"]["POSITION"].get_uint64().value())["count"].get_uint64().value();
            EXPECT_EQ(index_count % 3, 0u);
            EXPECT_EQ(primitive["attributes"]["NORMAL"].error(),  simdjson::SUCCESS);
            EXPECT_EQ(primitive["attributes"]["TANGENT"].error(), simdjson::SUCCESS);

            const simdjson::dom::element topology = extensions["ERHE_mesh_topology"];
            const uint64_t point_count = topology["point_count"].get_uint64().value();
            EXPECT_GE(point_count, 1u);
            EXPECT_LE(point_count, position_count);
            check_raw_view(topology["vertex_points"].get_uint64().value(), 4 * position_count, "vertex_points");

            simdjson::dom::element bvh;
            if (extensions["ERHE_raytrace_bvh"].get(bvh) != simdjson::SUCCESS) {
                EXPECT_FALSE(has_bvh_backend);
                continue;
            }
            EXPECT_EQ(bvh["triangle_count"].get_uint64().value(), index_count / 3);
            const simdjson::dom::element build = bvh["builds"].get_array().at(0);
            EXPECT_EQ(std::string_view{build["backend"].get_string().value()}, erhe::raytrace::get_prebuilt_bvh_backend());
            EXPECT_EQ(std::string_view{build["library"].get_string().value()}, erhe::raytrace::get_prebuilt_bvh_library());
            const uint64_t node_count = build["node_count"].get_uint64().value();
            check_raw_view(build["nodes"].get_uint64().value(),         28 * node_count,        "nodes");
            check_raw_view(build["primitive_ids"].get_uint64().value(), 4 * (index_count / 3), "primitive_ids");
        }
    }
    EXPECT_EQ(prepared_primitive_count, 2u);

    // The file loads with the extensions ignored: two quads, four welded
    // vertices and two triangles each.
    erhe::scene::Scene          loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("prepared.glb", glb), loaded);
    const std::shared_ptr<erhe::scene::Node> panel = find_by_name(data.nodes, "Panel");
    ASSERT_TRUE(panel);
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(panel.get());
    ASSERT_TRUE(mesh);
    ASSERT_EQ(mesh->get_primitives().size(), 2u);
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
        const std::shared_ptr<erhe::primitive::Triangle_soup>& soup = mesh_primitive.primitive->render_shape->get_triangle_soup();
        ASSERT_TRUE(soup);
        EXPECT_EQ(soup->get_vertex_count(), 4u);
        EXPECT_EQ(soup->get_index_count(), 6u);
    }
}

// A GLB split into its JSON and BIN chunks, and put back together, so tests
// can tamper with either.
class Glb_chunks
{
public:
    std::string json;
    std::string bin;
};

[[nodiscard]] auto split_glb(const std::string& glb) -> Glb_chunks
{
    Glb_chunks chunks;
    uint32_t   json_length = 0;
    std::memcpy(&json_length, glb.data() + 12, sizeof(uint32_t));
    chunks.json = glb.substr(20, json_length);
    const std::size_t bin_header = 20 + static_cast<std::size_t>(json_length);
    if (bin_header + 8 <= glb.size()) {
        uint32_t bin_length = 0;
        std::memcpy(&bin_length, glb.data() + bin_header, sizeof(uint32_t));
        chunks.bin = glb.substr(bin_header + 8, bin_length);
    }
    return chunks;
}

[[nodiscard]] auto join_glb(const Glb_chunks& chunks) -> std::string
{
    std::string json = chunks.json;
    while ((json.size() % 4) != 0) {
        json += ' ';
    }
    std::string bin = chunks.bin;
    while ((bin.size() % 4) != 0) {
        bin += '\0';
    }
    const auto put_u32 = [](std::string& out, const uint32_t value) {
        char bytes[4];
        std::memcpy(bytes, &value, sizeof(uint32_t));
        out.append(bytes, 4);
    };
    std::string glb;
    put_u32(glb, 0x46546C67u); // "glTF"
    put_u32(glb, 2u);
    put_u32(glb, static_cast<uint32_t>(12 + 8 + json.size() + (bin.empty() ? 0 : (8 + bin.size()))));
    put_u32(glb, static_cast<uint32_t>(json.size()));
    put_u32(glb, 0x4E4F534Au); // "JSON"
    glb += json;
    if (!bin.empty()) {
        put_u32(glb, static_cast<uint32_t>(bin.size()));
        put_u32(glb, 0x004E4942u); // "BIN\0"
        glb += bin;
    }
    return glb;
}

[[nodiscard]] auto parse_glb(
    const std::string&                       glb,
    const std::string_view                   file_name,
    erhe::scene::Scene&                      scene,
    const erhe::gltf::Prepared_mesh_data_use use
) -> erhe::gltf::Gltf_data
{
    tf::Executor executor{1};
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    return erhe::gltf::parse_gltf(
        erhe::gltf::Gltf_parse_arguments{
            .executor               = executor,
            .root_node              = root_node,
            .path                   = write_temporary_file(file_name, glb),
            .parallel               = false,
            .prepared_mesh_data_use = use
        }
    );
}

// variants.gltf written with prepared mesh data: two soup primitives.
[[nodiscard]] auto make_prepared_glb() -> std::string
{
    erhe::scene::Scene          scene{"source", nullptr};
    const erhe::gltf::Gltf_data source = parse_file(data_path("variants.gltf"), scene);
    EXPECT_EQ(source.nodes.size(), 1u);
    const erhe::gltf::Gltf_export_arguments arguments{
        .root_node          = *scene.get_root_node(),
        .binary             = true,
        .prepared_mesh_data = erhe::gltf::Prepared_mesh_data::write
    };
    return erhe::gltf::export_gltf(arguments);
}

// The render shapes of the "Panel" mesh of a parsed variants.gltf copy.
[[nodiscard]] auto get_panel_shapes(const erhe::gltf::Gltf_data& data) -> std::vector<std::shared_ptr<erhe::primitive::Primitive_render_shape>>
{
    std::vector<std::shared_ptr<erhe::primitive::Primitive_render_shape>> shapes;
    const std::shared_ptr<erhe::scene::Node> panel = find_by_name(data.nodes, "Panel");
    EXPECT_TRUE(panel);
    if (!panel) {
        return shapes;
    }
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(panel.get());
    EXPECT_TRUE(mesh);
    if (!mesh) {
        return shapes;
    }
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
        shapes.push_back(mesh_primitive.primitive->render_shape);
        // Every primitive loads as a soup, whatever was accepted.
        const std::shared_ptr<erhe::primitive::Triangle_soup>& soup = mesh_primitive.primitive->render_shape->get_triangle_soup();
        EXPECT_TRUE(soup);
        if (soup) {
            EXPECT_EQ(soup->get_vertex_count(), 4u);
            EXPECT_EQ(soup->get_index_count(), 6u);
        }
    }
    return shapes;
}

// Replaces the first occurrence of `key` + value text in the JSON chunk:
// the `length` characters right after `key`.
void replace_after(std::string& json, const std::string_view key, const std::size_t length, const std::string_view replacement)
{
    const std::size_t position = json.find(key);
    ASSERT_NE(position, std::string::npos) << key;
    json.replace(position + key.size(), length, replacement);
}

// The bufferView index the first `"<member>":N` in the JSON names.
[[nodiscard]] auto first_view_index(const std::string& json, const std::string_view member) -> uint64_t
{
    const std::string key      = "\"" + std::string{member} + "\":";
    const std::size_t position = json.find(key);
    EXPECT_NE(position, std::string::npos) << member;
    return std::stoull(json.substr(position + key.size(), 20));
}

// Prepared mesh data read back (doc/plans/gltf_fast_load.md step 5): every
// prepared primitive validates and carries its payloads; with the setting
// off nothing is read.
// Mesh_payload_load::deferred: the parse gives each soup primitive its source
// and counts only; Primitive::load_payload() then reads the soup and
// validates the prepared mesh data, with the same result as a parse that
// reads them itself. A file changed before the load refuses it.
TEST(Gltf_roundtrip, deferred_mesh_payload_loads_like_the_parse)
{
    const std::string glb = make_prepared_glb();
    ASSERT_FALSE(glb.empty());
    const bool has_bvh_backend = !erhe::raytrace::get_prebuilt_bvh_backend().empty();

    erhe::scene::Scene          reference_scene{"reference", nullptr};
    const erhe::gltf::Gltf_data reference = parse_glb(glb, "deferred_reference.glb", reference_scene, erhe::gltf::Prepared_mesh_data_use::use);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive_render_shape>> reference_shapes = get_panel_shapes(reference);
    ASSERT_EQ(reference_shapes.size(), 2u);

    const auto parse_deferred = [&](const std::filesystem::path& path, erhe::scene::Scene& scene) -> erhe::gltf::Gltf_data {
        tf::Executor executor{1};
        const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
        return erhe::gltf::parse_gltf(
            erhe::gltf::Gltf_parse_arguments{
                .executor     = executor,
                .root_node    = root_node,
                .path         = path,
                .parallel     = false,
                .mesh_payload = erhe::gltf::Mesh_payload_load::deferred
            }
        );
    };
    const auto get_primitives = [](const erhe::gltf::Gltf_data& data) -> std::vector<std::shared_ptr<erhe::primitive::Primitive>> {
        std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives;
        const std::shared_ptr<erhe::scene::Node> panel = find_by_name(data.nodes, "Panel");
        EXPECT_TRUE(panel);
        if (panel) {
            for (const erhe::scene::Mesh_primitive& mesh_primitive : erhe::scene::get_mesh(panel.get())->get_primitives()) {
                primitives.push_back(mesh_primitive.primitive);
            }
        }
        return primitives;
    };

    const std::filesystem::path path = write_temporary_file("deferred_payload.glb", glb);
    erhe::scene::Scene    scene{"deferred", nullptr};
    erhe::gltf::Gltf_data data = parse_deferred(path, scene);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives = get_primitives(data);
    ASSERT_EQ(primitives.size(), 2u);
    for (const std::shared_ptr<erhe::primitive::Primitive>& primitive : primitives) {
        ASSERT_TRUE(primitive->render_shape);
        EXPECT_TRUE(primitive->render_shape->is_payload_pending());
        EXPECT_FALSE(primitive->is_prepared());
        EXPECT_EQ(primitive->render_shape->get_triangle_soup_counts().vertex_count, 4u);
        EXPECT_EQ(primitive->render_shape->get_triangle_soup_counts().index_count,  6u);
    }
    EXPECT_EQ(data.prepared_mesh_statistics.used_count, 0u); // nothing validated yet

    for (std::size_t i = 0; i < primitives.size(); ++i) {
        ASSERT_TRUE(primitives[i]->load_payload());
        EXPECT_FALSE(primitives[i]->render_shape->is_payload_pending());
        EXPECT_TRUE(primitives[i]->is_prepared());
        EXPECT_EQ(primitives[i]->render_shape->has_stored_bvh(), has_bvh_backend);
        const std::shared_ptr<erhe::primitive::Triangle_soup> soup      = primitives[i]->render_shape->get_triangle_soup();
        const std::shared_ptr<erhe::primitive::Triangle_soup> reference_soup = reference_shapes[i]->get_triangle_soup();
        ASSERT_TRUE(soup);
        ASSERT_TRUE(reference_soup);
        EXPECT_EQ(soup->vertex_data, reference_soup->vertex_data);
        EXPECT_EQ(soup->index_data,  reference_soup->index_data);
        const std::shared_ptr<const erhe::primitive::Prepared_primitive_data>& prepared           = primitives[i]->render_shape->get_prepared_data();
        const std::shared_ptr<const erhe::primitive::Prepared_primitive_data>& reference_prepared = reference_shapes[i]->get_prepared_data();
        ASSERT_TRUE(prepared);
        ASSERT_TRUE(reference_prepared);
        EXPECT_EQ(prepared->content_hash, reference_prepared->content_hash);
        ASSERT_TRUE(prepared->colocation.has_value());
        EXPECT_EQ(prepared->colocation->vertex_points, reference_prepared->colocation->vertex_points);
    }
    erhe::gltf::finish_prepared_mesh_statistics(data, path);
    EXPECT_EQ(data.prepared_mesh_statistics.used_count,          2u);
    EXPECT_EQ(data.prepared_mesh_statistics.rejected_count,      0u);
    EXPECT_EQ(data.prepared_mesh_statistics.topology_used_count, 2u);

    // A file that changed between the parse and the load: refused.
    const std::filesystem::path changed_path = write_temporary_file("deferred_payload_changed.glb", glb);
    erhe::scene::Scene    changed_scene{"changed", nullptr};
    const erhe::gltf::Gltf_data changed_data = parse_deferred(changed_path, changed_scene);
    const std::filesystem::file_time_type old_time = std::filesystem::last_write_time(changed_path);
    static_cast<void>(write_temporary_file("deferred_payload_changed.glb", glb));
    std::filesystem::last_write_time(changed_path, old_time + std::chrono::seconds{2});
    for (const std::shared_ptr<erhe::primitive::Primitive>& primitive : get_primitives(changed_data)) {
        EXPECT_FALSE(primitive->load_payload());
        EXPECT_TRUE(primitive->render_shape->is_payload_pending());
    }
}

// A save onto the file the released soups read from: before the rebind the
// changed file refuses the reload; rebind_sources_to_written_file() gives
// each retained primitive a source in the new file, and the soups read back
// bit-identical. A primitive whose source is detached before the save keeps
// its soup resident and has no source afterwards.
TEST(Gltf_roundtrip, save_onto_source_rebinds_released_soups)
{
    const std::string glb = make_prepared_glb();
    ASSERT_FALSE(glb.empty());
    const std::filesystem::path path = write_temporary_file("rebind_source.glb", glb);

    erhe::scene::Scene scene{"rebind", nullptr};
    tf::Executor executor{1};
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    const erhe::gltf::Gltf_data data = erhe::gltf::parse_gltf(
        erhe::gltf::Gltf_parse_arguments{
            .executor             = executor,
            .root_node            = root_node,
            .path                 = path,
            .parallel             = false,
            .triangle_soup_reload = erhe::gltf::Triangle_soup_reload::record
        }
    );
    const std::shared_ptr<erhe::scene::Node> panel = find_by_name(data.nodes, "Panel");
    ASSERT_TRUE(panel);
    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = erhe::scene::get_mesh(panel.get())->get_primitives();
    ASSERT_EQ(mesh_primitives.size(), 2u);
    std::vector<erhe::primitive::Triangle_soup> originals;
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
        originals.push_back(*mesh_primitive.primitive->render_shape->get_triangle_soup());
        EXPECT_TRUE(mesh_primitive.primitive->render_shape->get_triangle_soup_source()->references_file(path));
    }
    // Primitive 0 released (its soup goes back to the file), primitive 1
    // detached (its soup made resident, no source).
    ASSERT_TRUE(mesh_primitives[0].primitive->release_triangle_soup());
    ASSERT_TRUE(mesh_primitives[1].primitive->detach_triangle_soup_source());
    EXPECT_FALSE(mesh_primitives[1].primitive->render_shape->get_triangle_soup_source());

    erhe::gltf::Gltf_export_written_layout layout;
    erhe::gltf::Gltf_export_arguments arguments{
        .root_node          = *scene.get_root_node(),
        .binary             = true,
        .prepared_mesh_data = erhe::gltf::Prepared_mesh_data::write
    };
    arguments.written_layout = &layout;
    const std::string saved = erhe::gltf::export_gltf(arguments);
    ASSERT_FALSE(saved.empty());
    // The file is new: a different modification time.
    const std::filesystem::file_time_type old_time = std::filesystem::last_write_time(path);
    static_cast<void>(write_temporary_file("rebind_source.glb", saved));
    std::filesystem::last_write_time(path, old_time + std::chrono::seconds{2});

    EXPECT_FALSE(mesh_primitives[0].primitive->render_shape->get_triangle_soup()); // refused: file changed
    EXPECT_GE(layout.retained_primitives.size(), 1u);

    const erhe::gltf::Gltf_rebind_result result = erhe::gltf::rebind_sources_to_written_file(path, layout);
    EXPECT_EQ(result.primitive_count, 1u); // the detached primitive has no source to rebind
    EXPECT_EQ(result.failed_count, 0u);
    const std::shared_ptr<erhe::primitive::Triangle_soup> reloaded = mesh_primitives[0].primitive->render_shape->get_triangle_soup();
    ASSERT_TRUE(reloaded);
    EXPECT_EQ(reloaded->vertex_data, originals[0].vertex_data);
    EXPECT_EQ(reloaded->index_data,  originals[0].index_data);
    const std::shared_ptr<erhe::primitive::Triangle_soup> resident = mesh_primitives[1].primitive->render_shape->get_triangle_soup();
    ASSERT_TRUE(resident);
    EXPECT_EQ(resident->vertex_data, originals[1].vertex_data);
}

// The vertex layouts Mesh_memory derives on a device with position
// quantization and unorm16x3 vertex input (doc/gltf_extensions/ERHE_gpu_vertex_streams.md
// "Signature"), built here without a device.
[[nodiscard]] auto make_test_gpu_formats() -> std::pair<erhe::dataformat::Vertex_format, erhe::dataformat::Vertex_format>
{
    using erhe::dataformat::Format;
    using erhe::dataformat::Vertex_attribute_usage;
    erhe::dataformat::Vertex_format base{
        {
            {0, {{Format::format_32_vec3_float, Vertex_attribute_usage::position, 0}}},
            {1, {
                {Format::format_32_vec3_float, Vertex_attribute_usage::normal,    erhe::dataformat::normal_attribute},
                {Format::format_32_vec4_float, Vertex_attribute_usage::tangent,   0},
                {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0},
                {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 1},
                {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 2},
                {Format::format_32_vec4_float, Vertex_attribute_usage::color,     0}
            }},
            {2, {
                {Format::format_32_vec3_float, Vertex_attribute_usage::normal, erhe::dataformat::normal_attribute_smooth},
                {Format::format_8_vec2_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_aniso_control},
                {Format::format_16_vec2_uint,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_valency_edge_count},
                {Format::format_8_vec4_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_id}
            }}
        }
    };
    erhe::dataformat::Vertex_format optimized{
        {
            {0, {{Format::format_16_vec3_snorm, Vertex_attribute_usage::position, 0}}},
            {1, {
                {Format::format_16_vec4_sint,  Vertex_attribute_usage::tangent,   0},
                {Format::format_16_vec2_unorm, Vertex_attribute_usage::tex_coord, 0},
                {Format::format_16_vec2_unorm, Vertex_attribute_usage::tex_coord, 1},
                {Format::format_16_vec2_unorm, Vertex_attribute_usage::tex_coord, 2},
                {Format::format_8_vec4_unorm,  Vertex_attribute_usage::color,     0}
            }},
            {2, {{Format::format_8_vec2_unorm, Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_aniso_control}}}
        }
    };
    const erhe::dataformat::Vertex_stream_packing packing{.min_attribute_alignment = 1, .min_stride_alignment = 4};
    base.repack(packing);
    optimized.repack(packing);
    return {std::move(base), std::move(optimized)};
}

// "Save for Quest" GPU vertex streams: each prepared primitive's base
// streams are its core accessors and its optimized streams are named by
// ERHE_gpu_vertex_streams; the stored optimized bytes are exactly what the
// loader's pack of the core writes, and a loader that ignores the extension
// reads the same soup as from a plain prepared save.
TEST(Gltf_roundtrip, gpu_vertex_streams_hold_the_loader_pack)
{
    erhe::scene::Scene          source_scene{"source", nullptr};
    const erhe::gltf::Gltf_data source = erhe_gltf_test::parse_file(data_path("variants.gltf"), source_scene);
    static_cast<void>(source);
    const auto [base_format, optimized_format] = make_test_gpu_formats();
    const erhe::gltf::Gltf_gpu_vertex_formats formats{
        .base              = &base_format,
        .base_skinned      = &base_format,
        .optimized         = &optimized_format,
        .optimized_skinned = &optimized_format,
        .index_type        = erhe::dataformat::Format::format_32_scalar_uint,
        .encoding_version  = erhe::primitive::vertex_encoding_version
    };
    erhe::gltf::Gltf_export_prepared_statistics statistics;
    erhe::gltf::Gltf_export_arguments arguments{
        .root_node          = *source_scene.get_root_node(),
        .binary             = true,
        .prepared_mesh_data = erhe::gltf::Prepared_mesh_data::write
    };
    arguments.prepared_statistics = &statistics;
    arguments.gpu_vertex_formats  = &formats;
    const std::string glb = erhe::gltf::export_gltf(arguments);
    ASSERT_FALSE(glb.empty());
    EXPECT_EQ(statistics.gpu_vertex_stream_count, 2u);

    // A loader that ignores the extension reads the same soups as from a plain
    // prepared save.
    erhe::scene::Scene          plain_scene{"plain", nullptr};
    const erhe::gltf::Gltf_data plain = parse_glb(make_prepared_glb(), "gpu_streams_plain.glb", plain_scene, erhe::gltf::Prepared_mesh_data_use::use);
    erhe::scene::Scene          gpu_scene{"gpu", nullptr};
    const erhe::gltf::Gltf_data gpu = parse_glb(glb, "gpu_streams.glb", gpu_scene, erhe::gltf::Prepared_mesh_data_use::use);
    EXPECT_EQ(gpu.prepared_mesh_statistics.used_count, 2u);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive_render_shape>> plain_shapes = get_panel_shapes(plain);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive_render_shape>> gpu_shapes   = get_panel_shapes(gpu);
    ASSERT_EQ(plain_shapes.size(), 2u);
    ASSERT_EQ(gpu_shapes.size(),   2u);

    // The stored optimized streams equal the pack of the core, byte for byte.
    const Glb_chunks             chunks = split_glb(glb);
    simdjson::dom::parser        parser;
    const simdjson::dom::element root   = parser.parse(chunks.json);
    bool used = false;
    for (const simdjson::dom::element name : root["extensionsUsed"].get_array()) {
        used = used || (name.get_string().value() == "ERHE_gpu_vertex_streams");
    }
    EXPECT_TRUE(used);
    const simdjson::dom::array buffer_views = root["bufferViews"].get_array();
    const simdjson::dom::array primitives   = root["meshes"].at(0)["primitives"].get_array();
    ASSERT_EQ(primitives.size(), 2u);
    for (std::size_t p = 0; p < 2; ++p) {
        const std::shared_ptr<erhe::primitive::Triangle_soup> plain_soup = plain_shapes[p]->get_triangle_soup();
        const std::shared_ptr<erhe::primitive::Triangle_soup> gpu_soup   = gpu_shapes[p]->get_triangle_soup();
        ASSERT_TRUE(plain_soup);
        ASSERT_TRUE(gpu_soup);
        EXPECT_EQ(gpu_soup->vertex_data, plain_soup->vertex_data);
        EXPECT_EQ(gpu_soup->index_data,  plain_soup->index_data);

        const simdjson::dom::element extension = primitives.at(p)["extensions"]["ERHE_gpu_vertex_streams"];
        EXPECT_EQ(extension["version"].get_uint64().value(), 1u);
        const simdjson::dom::element optimized = extension["variants"].at(1);
        EXPECT_EQ(optimized["role"].get_string().value(), "optimized");
        EXPECT_EQ(optimized["signature"].get_string().value(), erhe::dataformat::get_vertex_format_signature(optimized_format));
        const std::optional<erhe::primitive::Packed_vertex_streams> pack =
            erhe::primitive::pack_triangle_soup_streams(*gpu_soup, optimized_format, erhe::dataformat::Format::format_32_scalar_uint);
        ASSERT_TRUE(pack.has_value());
        const simdjson::dom::array streams = optimized["streams"].get_array();
        ASSERT_EQ(streams.size(), optimized_format.streams.size());
        for (std::size_t i = 0; i < streams.size(); ++i) {
            std::uint64_t view_index = 0;
            if (streams.at(i)["bufferView"].get_uint64().get(view_index) != simdjson::SUCCESS) {
                EXPECT_EQ(streams.at(i)["fill"].get_string().value(), "zero");
                for (const uint8_t value : pack->streams[i]) {
                    EXPECT_EQ(value, 0u);
                }
                continue;
            }
            const simdjson::dom::element view = buffer_views.at(view_index);
            const std::uint64_t offset = view["byteOffset"].get_uint64().value();
            const std::uint64_t length = view["byteLength"].get_uint64().value();
            ASSERT_EQ(length, pack->streams[i].size());
            EXPECT_EQ(std::memcmp(chunks.bin.data() + offset, pack->streams[i].data(), length), 0) << "stream " << i;
        }
        // The affines are hexadecimal floats, exact to the bit.
        const simdjson::dom::element position = optimized["attributes"]["POSITION"];
        const auto hex_float = [](const simdjson::dom::element element) -> float {
            const std::string text{element.get_string().value()};
            return static_cast<float>(std::strtod(text.c_str(), nullptr));
        };
        for (glm::length_t c = 0; c < 3; ++c) {
            const float scale  = hex_float(position["scale" ].at(static_cast<std::size_t>(c)));
            const float offset = hex_float(position["offset"].at(static_cast<std::size_t>(c)));
            EXPECT_EQ(std::memcmp(&scale,  &pack->position_quantization.scale [c], sizeof(float)), 0);
            EXPECT_EQ(std::memcmp(&offset, &pack->position_quantization.offset[c], sizeof(float)), 0);
        }
    }
}

// Records what a build enqueues: every allocation gets its own pool id at
// offset 0 (the lockstep check sees one buffer), the bytes kept by pool id.
class Recording_buffer_sink
    : public erhe::primitive::Vertex_buffer_sink
    , public erhe::primitive::Index_buffer_sink
{
public:
    auto allocate_vertex_buffer_range(const erhe::dataformat::Vertex_stream& vertex_stream, const std::size_t vertex_count) -> erhe::primitive::Buffer_sink_allocation override
    {
        return allocate(vertex_count, vertex_stream.stride);
    }
    void enqueue_vertex_data(const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data) override
    {
        bytes.at(buffer_range.pool_id) = std::move(data);
    }
    void vertex_writer_ready(erhe::primitive::Vertex_buffer_writer&) override
    {
    }
    auto allocate_index_buffer_range(const erhe::dataformat::Format index_format, const std::size_t index_count) -> erhe::primitive::Buffer_sink_allocation override
    {
        return allocate(index_count, erhe::dataformat::get_format_size_bytes(index_format));
    }
    void enqueue_index_data(const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data) override
    {
        bytes.at(buffer_range.pool_id) = std::move(data);
    }
    void index_writer_ready(erhe::primitive::Index_buffer_writer&) override
    {
    }

    [[nodiscard]] auto get(const erhe::primitive::Buffer_range& range) const -> const std::vector<uint8_t>&
    {
        return bytes.at(range.pool_id);
    }

    std::vector<std::vector<uint8_t>> bytes;

private:
    auto allocate(const std::size_t count, const std::size_t element_size) -> erhe::primitive::Buffer_sink_allocation
    {
        bytes.emplace_back();
        return erhe::primitive::Buffer_sink_allocation{
            .range = erhe::primitive::Buffer_range{
                .count        = count,
                .element_size = element_size,
                .byte_offset  = 0,
                .pool_id      = bytes.size() - 1,
                .buffer_id    = 0
            },
            .allocation = {}
        };
    }
};

// The uploaded bytes and the decoder-facing fields of two builds of one
// variant are identical: indices, every vertex stream, the stored affines
// (memcmp, bit for bit), bounds and the color flag.
void expect_same_buffer_mesh(
    const erhe::primitive::Buffer_mesh& a, const Recording_buffer_sink& a_sink,
    const erhe::primitive::Buffer_mesh& b, const Recording_buffer_sink& b_sink
)
{
    EXPECT_EQ(a_sink.get(a.index_buffer_range), b_sink.get(b.index_buffer_range));
    ASSERT_EQ(a.vertex_buffer_ranges.size(), b.vertex_buffer_ranges.size());
    for (std::size_t i = 0; i < a.vertex_buffer_ranges.size(); ++i) {
        EXPECT_EQ(a_sink.get(a.vertex_buffer_ranges[i]), b_sink.get(b.vertex_buffer_ranges[i])) << "stream " << i;
    }
    EXPECT_EQ(std::memcmp(&a.position_quantization, &b.position_quantization, sizeof(a.position_quantization)), 0);
    EXPECT_EQ(std::memcmp(&a.texcoord_quantization, &b.texcoord_quantization, sizeof(a.texcoord_quantization)), 0);
    EXPECT_EQ(std::memcmp(&a.bounding_box,          &b.bounding_box,          sizeof(a.bounding_box)), 0);
    EXPECT_EQ(std::memcmp(&a.bounding_sphere,       &b.bounding_sphere,       sizeof(a.bounding_sphere)), 0);
    EXPECT_EQ(a.has_vertex_colors, b.has_vertex_colors);
    EXPECT_EQ(a.triangle_fill_indices.index_count, b.triangle_fill_indices.index_count);
    EXPECT_EQ(a.vertex_input_key, b.vertex_input_key);
}

// The loader's stored-stream build (Primitive::make_stored_meshes()) gives
// the same GPU data as the soup build of the same file: the bytes, the
// affines read back bit-exactly from the hexadecimal floats, the bounds;
// a layout that differs from the stored signature is declined and the soup
// path still loads.
TEST(Gltf_roundtrip, stored_vertex_streams_build_like_the_soup)
{
    erhe::scene::Scene          source_scene{"source", nullptr};
    const erhe::gltf::Gltf_data source = erhe_gltf_test::parse_file(data_path("variants.gltf"), source_scene);
    static_cast<void>(source);
    const auto [base_format, optimized_format] = make_test_gpu_formats();
    const erhe::gltf::Gltf_gpu_vertex_formats formats{
        .base              = &base_format,
        .base_skinned      = &base_format,
        .optimized         = &optimized_format,
        .optimized_skinned = &optimized_format,
        .index_type        = erhe::dataformat::Format::format_32_scalar_uint,
        .encoding_version  = erhe::primitive::vertex_encoding_version
    };
    erhe::gltf::Gltf_export_arguments arguments{
        .root_node          = *source_scene.get_root_node(),
        .binary             = true,
        .prepared_mesh_data = erhe::gltf::Prepared_mesh_data::write
    };
    arguments.gpu_vertex_formats = &formats;
    const std::string glb = erhe::gltf::export_gltf(arguments);
    ASSERT_FALSE(glb.empty());
    const std::filesystem::path path = write_temporary_file("stored_streams.glb", glb);

    const auto parse_deferred = [&](erhe::scene::Scene& scene) -> erhe::gltf::Gltf_data {
        tf::Executor executor{1};
        return erhe::gltf::parse_gltf(
            erhe::gltf::Gltf_parse_arguments{
                .executor     = executor,
                .root_node    = scene.get_root_node(),
                .path         = path,
                .parallel     = false,
                .mesh_payload = erhe::gltf::Mesh_payload_load::deferred
            }
        );
    };
    const auto get_primitives = [](const erhe::gltf::Gltf_data& data) -> std::vector<std::shared_ptr<erhe::primitive::Primitive>> {
        std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives;
        const std::shared_ptr<erhe::scene::Node> panel = find_by_name(data.nodes, "Panel");
        EXPECT_TRUE(panel);
        if (panel) {
            for (const erhe::scene::Mesh_primitive& mesh_primitive : erhe::scene::get_mesh(panel.get())->get_primitives()) {
                primitives.push_back(mesh_primitive.primitive);
            }
        }
        return primitives;
    };
    const auto make_build_info = [&](Recording_buffer_sink& sink, const erhe::dataformat::Vertex_format& optimized) -> erhe::primitive::Build_info {
        erhe::primitive::Build_info build_info{
            .primitive_types = {.fill_triangles = true},
            .buffer_info     = {
                .normal_style       = erhe::primitive::Normal_style::corner_normals,
                .index_type         = erhe::dataformat::Format::format_32_scalar_uint,
                .vertex_format      = base_format,
                .vertex_buffer_sink = sink,
                .index_buffer_sink  = sink,
                .vertex_input_key   = 1
            }
        };
        build_info.buffer_info.optimize_meshes            = true;
        build_info.buffer_info.optimized_vertex_format    = &optimized;
        build_info.buffer_info.optimized_vertex_input_key = 2;
        return build_info;
    };

    erhe::scene::Scene    stored_scene{"stored", nullptr};
    erhe::gltf::Gltf_data stored_data = parse_deferred(stored_scene);
    erhe::scene::Scene    soup_scene{"soup", nullptr};
    erhe::gltf::Gltf_data soup_data = parse_deferred(soup_scene);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> stored_primitives = get_primitives(stored_data);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> soup_primitives   = get_primitives(soup_data);
    ASSERT_EQ(stored_primitives.size(), 2u);
    ASSERT_EQ(soup_primitives.size(),   2u);

    Recording_buffer_sink             stored_sink;
    Recording_buffer_sink             soup_sink;
    const erhe::primitive::Build_info stored_build_info = make_build_info(stored_sink, optimized_format);
    const erhe::primitive::Build_info soup_build_info   = make_build_info(soup_sink,   optimized_format);
    for (std::size_t i = 0; i < 2; ++i) {
        std::string reason;
        EXPECT_EQ(stored_primitives[i]->make_stored_meshes(stored_build_info, erhe::primitive::Base_mesh_build::at_load, reason), erhe::primitive::Primitive::Stored_build_result::built) << reason;
        EXPECT_FALSE(stored_primitives[i]->render_shape->is_payload_pending());
        EXPECT_TRUE (stored_primitives[i]->is_prepared());
        EXPECT_TRUE (stored_primitives[i]->render_shape->is_triangle_soup_released());
        ASSERT_TRUE (stored_primitives[i]->optimized_render_shape);

        ASSERT_TRUE(soup_primitives[i]->load_payload());
        ASSERT_TRUE(soup_primitives[i]->make_prepared_meshes(soup_build_info));
        ASSERT_TRUE(soup_primitives[i]->optimized_render_shape);

        expect_same_buffer_mesh(
            stored_primitives[i]->render_shape->get_renderable_mesh(), stored_sink,
            soup_primitives  [i]->render_shape->get_renderable_mesh(), soup_sink
        );
        expect_same_buffer_mesh(
            stored_primitives[i]->optimized_render_shape->get_renderable_mesh(), stored_sink,
            soup_primitives  [i]->optimized_render_shape->get_renderable_mesh(), soup_sink
        );
        EXPECT_EQ(stored_primitives[i]->has_real_raytrace(), soup_primitives[i]->has_real_raytrace());
        if (stored_primitives[i]->has_real_raytrace()) {
            const erhe::primitive::Buffer_mesh& a = stored_primitives[i]->render_shape->get_raytrace().get_raytrace_mesh();
            const erhe::primitive::Buffer_mesh& b = soup_primitives  [i]->render_shape->get_raytrace().get_raytrace_mesh();
            EXPECT_EQ(std::memcmp(&a.bounding_box,    &b.bounding_box,    sizeof(a.bounding_box)), 0);
            EXPECT_EQ(std::memcmp(&a.bounding_sphere, &b.bounding_sphere, sizeof(a.bounding_sphere)), 0);
        }
        // The released soup reads back as the soup path's.
        const std::shared_ptr<erhe::primitive::Triangle_soup> stored_soup = stored_primitives[i]->render_shape->get_triangle_soup();
        const std::shared_ptr<erhe::primitive::Triangle_soup> soup        = soup_primitives  [i]->render_shape->get_triangle_soup();
        ASSERT_TRUE(stored_soup);
        ASSERT_TRUE(soup);
        EXPECT_EQ(stored_soup->vertex_data, soup->vertex_data);
        EXPECT_EQ(stored_soup->index_data,  soup->index_data);
    }

    // Another optimized layout (unquantized positions): declined, untouched,
    // and the soup path still loads.
    erhe::dataformat::Vertex_format other_optimized = optimized_format;
    other_optimized.streams[0].attributes[0].format = erhe::dataformat::Format::format_32_vec3_float;
    other_optimized.repack(erhe::dataformat::Vertex_stream_packing{.min_attribute_alignment = 1, .min_stride_alignment = 4});
    erhe::scene::Scene    other_scene{"other", nullptr};
    erhe::gltf::Gltf_data other_data = parse_deferred(other_scene);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> other_primitives = get_primitives(other_data);
    ASSERT_EQ(other_primitives.size(), 2u);
    Recording_buffer_sink             other_sink;
    const erhe::primitive::Build_info other_build_info = make_build_info(other_sink, other_optimized);
    std::string reason;
    EXPECT_EQ(other_primitives[0]->make_stored_meshes(other_build_info, erhe::primitive::Base_mesh_build::at_load, reason), erhe::primitive::Primitive::Stored_build_result::declined);
    EXPECT_EQ(reason, "optimized signature differs");
    EXPECT_TRUE(other_primitives[0]->render_shape->is_payload_pending());
    ASSERT_TRUE(other_primitives[0]->load_payload());
    EXPECT_TRUE(other_primitives[0]->make_prepared_meshes(other_build_info));
}

// A deferred base variant (Base_mesh_build::deferred, doc/plans/gltf_fast_load.md
// "Item 10"): the stored build uploads only the optimized variant and leaves
// a descriptor base (bounds and affines, no ranges); the base-only build
// later uploads exactly what the at-load build uploads.
TEST(Gltf_roundtrip, deferred_base_variant_builds_on_demand_like_at_load)
{
    erhe::scene::Scene          source_scene{"source", nullptr};
    const erhe::gltf::Gltf_data source = erhe_gltf_test::parse_file(data_path("variants.gltf"), source_scene);
    static_cast<void>(source);
    const auto [base_format, optimized_format] = make_test_gpu_formats();
    const erhe::gltf::Gltf_gpu_vertex_formats formats{
        .base              = &base_format,
        .base_skinned      = &base_format,
        .optimized         = &optimized_format,
        .optimized_skinned = &optimized_format,
        .index_type        = erhe::dataformat::Format::format_32_scalar_uint,
        .encoding_version  = erhe::primitive::vertex_encoding_version
    };
    erhe::gltf::Gltf_export_arguments arguments{
        .root_node          = *source_scene.get_root_node(),
        .binary             = true,
        .prepared_mesh_data = erhe::gltf::Prepared_mesh_data::write
    };
    arguments.gpu_vertex_formats = &formats;
    const std::string glb = erhe::gltf::export_gltf(arguments);
    ASSERT_FALSE(glb.empty());
    const std::filesystem::path path = write_temporary_file("deferred_base.glb", glb);
    const auto parse_deferred = [&](erhe::scene::Scene& scene) -> std::vector<std::shared_ptr<erhe::primitive::Primitive>> {
        tf::Executor executor{1};
        const erhe::gltf::Gltf_data data = erhe::gltf::parse_gltf(
            erhe::gltf::Gltf_parse_arguments{
                .executor     = executor,
                .root_node    = scene.get_root_node(),
                .path         = path,
                .parallel     = false,
                .mesh_payload = erhe::gltf::Mesh_payload_load::deferred
            }
        );
        std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives;
        const std::shared_ptr<erhe::scene::Node> panel = find_by_name(data.nodes, "Panel");
        if (panel) {
            for (const erhe::scene::Mesh_primitive& mesh_primitive : erhe::scene::get_mesh(panel.get())->get_primitives()) {
                primitives.push_back(mesh_primitive.primitive);
            }
        }
        return primitives;
    };
    const auto make_build_info = [&](Recording_buffer_sink& sink) -> erhe::primitive::Build_info {
        erhe::primitive::Build_info build_info{
            .primitive_types = {.fill_triangles = true},
            .buffer_info     = {
                .normal_style       = erhe::primitive::Normal_style::corner_normals,
                .index_type         = erhe::dataformat::Format::format_32_scalar_uint,
                .vertex_format      = base_format,
                .vertex_buffer_sink = sink,
                .index_buffer_sink  = sink,
                .vertex_input_key   = 1
            }
        };
        build_info.buffer_info.optimize_meshes            = true;
        build_info.buffer_info.optimized_vertex_format    = &optimized_format;
        build_info.buffer_info.optimized_vertex_input_key = 2;
        return build_info;
    };

    erhe::scene::Scene deferred_scene{"deferred", nullptr};
    erhe::scene::Scene at_load_scene {"at_load",  nullptr};
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> deferred = parse_deferred(deferred_scene);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> at_load  = parse_deferred(at_load_scene);
    ASSERT_EQ(deferred.size(), 2u);
    ASSERT_EQ(at_load.size(),  2u);
    Recording_buffer_sink             deferred_sink;
    Recording_buffer_sink             at_load_sink;
    const erhe::primitive::Build_info deferred_build_info = make_build_info(deferred_sink);
    const erhe::primitive::Build_info at_load_build_info  = make_build_info(at_load_sink);
    for (std::size_t i = 0; i < 2; ++i) {
        std::string reason;
        ASSERT_EQ(deferred[i]->make_stored_meshes(deferred_build_info, erhe::primitive::Base_mesh_build::deferred, reason), erhe::primitive::Primitive::Stored_build_result::built) << reason;
        ASSERT_EQ(at_load [i]->make_stored_meshes(at_load_build_info,  erhe::primitive::Base_mesh_build::at_load,  reason), erhe::primitive::Primitive::Stored_build_result::built) << reason;
        const erhe::primitive::Primitive_render_shape& shape = *deferred[i]->render_shape;
        EXPECT_TRUE(shape.is_base_deferred());
        EXPECT_FALSE(shape.has_buffer_mesh_triangles());
        EXPECT_TRUE(shape.get_renderable_mesh().vertex_buffer_ranges.empty());
        const erhe::primitive::Buffer_mesh& reference = at_load[i]->render_shape->get_renderable_mesh();
        EXPECT_EQ(std::memcmp(&shape.get_renderable_mesh().position_quantization, &reference.position_quantization, sizeof(reference.position_quantization)), 0);
        EXPECT_EQ(std::memcmp(&shape.get_renderable_mesh().bounding_box,          &reference.bounding_box,          sizeof(reference.bounding_box)), 0);
        EXPECT_EQ(shape.get_renderable_mesh().vertex_input_key, reference.vertex_input_key);
        // Fill draws resolve to the optimized variant, identical to the at-load one.
        const auto resolved = deferred[i]->get_resolved_renderable_mesh(erhe::primitive::Mesh_variant::optimized, erhe::primitive::Primitive_mode::polygon_fill);
        ASSERT_EQ(resolved.first, erhe::primitive::Mesh_variant::optimized);
        expect_same_buffer_mesh(
            deferred[i]->optimized_render_shape->get_renderable_mesh(), deferred_sink,
            at_load [i]->optimized_render_shape->get_renderable_mesh(), at_load_sink
        );
        EXPECT_EQ(deferred[i]->has_real_raytrace(), at_load[i]->has_real_raytrace());

        // The base-only build: claimed once, staged, committed.
        erhe::primitive::Primitive_render_shape& mutable_shape = *deferred[i]->render_shape;
        EXPECT_TRUE (mutable_shape.claim_base_build_request());
        EXPECT_FALSE(mutable_shape.claim_base_build_request());
        ASSERT_TRUE(mutable_shape.prepare_deferred_base_mesh(deferred_build_info.buffer_info));
        EXPECT_TRUE(mutable_shape.is_base_deferred()); // staged, not committed
        ASSERT_TRUE(mutable_shape.commit_deferred_base_mesh());
        EXPECT_FALSE(mutable_shape.is_base_deferred());
        expect_same_buffer_mesh(mutable_shape.get_renderable_mesh(), deferred_sink, reference, at_load_sink);
    }
}

TEST(Gltf_roundtrip, prepared_mesh_data_parse_accepts_own_output)
{
    const std::string glb = make_prepared_glb();
    ASSERT_FALSE(glb.empty());
    const bool has_bvh_backend = !erhe::raytrace::get_prebuilt_bvh_backend().empty();

    erhe::scene::Scene          loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = parse_glb(glb, "prepared_parse.glb", loaded, erhe::gltf::Prepared_mesh_data_use::use);
    const erhe::gltf::Gltf_prepared_mesh_statistics& statistics = data.prepared_mesh_statistics;
    EXPECT_EQ(statistics.used_count,          2u);
    EXPECT_EQ(statistics.rejected_count,      0u);
    EXPECT_EQ(statistics.topology_used_count, 2u);
    EXPECT_EQ(statistics.bvh_used_count,      has_bvh_backend ? 2u : 0u);
    EXPECT_EQ(statistics.ignored_count,       0u);
    EXPECT_TRUE(statistics.rejection_reasons.empty());

    const std::vector<std::shared_ptr<erhe::primitive::Primitive_render_shape>> shapes = get_panel_shapes(data);
    ASSERT_EQ(shapes.size(), 2u);
    for (const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape : shapes) {
        const std::shared_ptr<const erhe::primitive::Prepared_primitive_data>& prepared = shape->get_prepared_data();
        ASSERT_TRUE(prepared);
        EXPECT_TRUE(prepared->options.weld);
        EXPECT_NE(prepared->content_hash, 0u);
        EXPECT_GT(prepared->bounding_sphere.radius, 0.0f);
        ASSERT_TRUE(prepared->colocation.has_value());
        EXPECT_EQ(prepared->colocation->vertex_points.size(), 4u);
        EXPECT_EQ(prepared->colocation->point_count, 4u);
        // The stored BVH moves into the shape at construction.
        EXPECT_FALSE(prepared->bvh.has_value());
        EXPECT_EQ(shape->has_stored_bvh(), has_bvh_backend);
    }

    erhe::scene::Scene          ignored{"ignored", nullptr};
    const erhe::gltf::Gltf_data ignored_data = parse_glb(glb, "prepared_ignored.glb", ignored, erhe::gltf::Prepared_mesh_data_use::ignore);
    EXPECT_EQ(ignored_data.prepared_mesh_statistics.ignored_count, 2u);
    EXPECT_EQ(ignored_data.prepared_mesh_statistics.used_count,    0u);
    for (const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape : get_panel_shapes(ignored_data)) {
        EXPECT_FALSE(shape->get_prepared_data());
    }
}

class Tamper_expectation
{
public:
    std::string reason;              // "<extension>: <reason>"
    bool        marker_accepted{false};
    bool        has_colocation {false};
    bool        has_bvh        {false};
};

// Parses a tampered copy: exactly one primitive is rejected, for `reason`,
// the other is fully used, and both still load as soups.
void expect_one_rejected(const std::string& glb, const std::string_view file_name, const Tamper_expectation& expectation)
{
    const bool has_bvh_backend = !erhe::raytrace::get_prebuilt_bvh_backend().empty();
    erhe::scene::Scene          loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = parse_glb(glb, file_name, loaded, erhe::gltf::Prepared_mesh_data_use::use);
    const erhe::gltf::Gltf_prepared_mesh_statistics& statistics = data.prepared_mesh_statistics;
    EXPECT_EQ(statistics.used_count,     1u) << file_name;
    EXPECT_EQ(statistics.rejected_count, 1u) << file_name;
    ASSERT_EQ(statistics.rejection_reasons.size(), 1u) << file_name;
    EXPECT_EQ(statistics.rejection_reasons.begin()->first,  expectation.reason) << file_name;
    EXPECT_EQ(statistics.rejection_reasons.begin()->second, 1u) << file_name;

    const std::vector<std::shared_ptr<erhe::primitive::Primitive_render_shape>> shapes = get_panel_shapes(data);
    ASSERT_EQ(shapes.size(), 2u);
    std::size_t full_count     = 0;
    std::size_t tampered_count = 0;
    for (const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape : shapes) {
        const std::shared_ptr<const erhe::primitive::Prepared_primitive_data>& prepared = shape->get_prepared_data();
        const bool full = prepared && prepared->colocation.has_value() && (shape->has_stored_bvh() == has_bvh_backend);
        if (full) {
            ++full_count;
            continue;
        }
        ++tampered_count;
        EXPECT_EQ(prepared != nullptr, expectation.marker_accepted) << file_name;
        if (prepared) {
            EXPECT_EQ(prepared->colocation.has_value(), expectation.has_colocation) << file_name;
            EXPECT_EQ(shape->has_stored_bvh(),          expectation.has_bvh)        << file_name;
        }
    }
    EXPECT_EQ(full_count,     1u) << file_name;
    EXPECT_EQ(tampered_count, 1u) << file_name;
}

// Tampered prepared payloads are rejected with the reason the spec's
// "Validation / fallback" section names, all-or-nothing per extension, and
// the primitive still loads as a plain soup.
TEST(Gltf_roundtrip, prepared_mesh_data_parse_rejects_tampered)
{
    const std::string glb = make_prepared_glb();
    ASSERT_FALSE(glb.empty());
    const bool       has_bvh_backend = !erhe::raytrace::get_prebuilt_bvh_backend().empty();
    const Glb_chunks original        = split_glb(glb);

    // Bad content_hash: the marker is rejected, which takes the topology and
    // the BVH with it.
    {
        Glb_chunks chunks = original;
        const std::string key = "\"content_hash\":\"";
        const std::size_t position = chunks.json.find(key);
        ASSERT_NE(position, std::string::npos);
        const char digit = chunks.json[position + key.size()];
        replace_after(chunks.json, key, 1, (digit == '0') ? "1" : "0");
        expect_one_rejected(join_glb(chunks), "tampered_hash.glb", Tamper_expectation{
            .reason = "ERHE_mesh_optimized: content_hash mismatch"
        });
    }

    // Bad vertex_points id: vertex 0 names point 1, which breaks the
    // first-reference order. The marker and the BVH stay in use.
    {
        Glb_chunks chunks = original;
        const uint64_t view_index = first_view_index(chunks.json, "vertex_points");
        simdjson::dom::parser  parser;
        const simdjson::dom::element root = parser.parse(chunks.json);
        const simdjson::dom::element view = root["bufferViews"].get_array().at(view_index);
        uint64_t byte_offset = 0;
        if (view["byteOffset"].get_uint64().get(byte_offset) != simdjson::SUCCESS) {
            byte_offset = 0;
        }
        ASSERT_EQ(view["buffer"].get_uint64().value(), 0u);
        const uint32_t bad_point = 1;
        ASSERT_LE(byte_offset + 4, chunks.bin.size());
        std::memcpy(chunks.bin.data() + byte_offset, &bad_point, sizeof(uint32_t));
        expect_one_rejected(join_glb(chunks), "tampered_vertex_points.glb", Tamper_expectation{
            .reason          = "ERHE_mesh_topology: not in first-reference order",
            .marker_accepted = true,
            .has_colocation  = false,
            .has_bvh         = has_bvh_backend
        });
    }

    // Truncated vertex_points view: its byteLength is 4 short (fastgltf writes
    // a bufferView as "buffer", "byteLength", "byteOffset").
    {
        Glb_chunks chunks = original;
        const uint64_t view_index = first_view_index(chunks.json, "vertex_points");
        simdjson::dom::parser  parser;
        const simdjson::dom::element root = parser.parse(chunks.json);
        const simdjson::dom::element view = root["bufferViews"].get_array().at(view_index);
        const uint64_t byte_offset = view["byteOffset"].get_uint64().value();
        const uint64_t byte_length = view["byteLength"].get_uint64().value();
        const std::string old_text = "\"byteLength\":" + std::to_string(byte_length) + ",\"byteOffset\":" + std::to_string(byte_offset);
        const std::string new_text = "\"byteLength\":" + std::to_string(byte_length - 4) + ",\"byteOffset\":" + std::to_string(byte_offset);
        const std::size_t position = chunks.json.find(old_text);
        ASSERT_NE(position, std::string::npos) << old_text;
        chunks.json.replace(position, old_text.size(), new_text);
        expect_one_rejected(join_glb(chunks), "tampered_truncated.glb", Tamper_expectation{
            .reason          = "ERHE_mesh_topology: bufferView byteLength mismatch",
            .marker_accepted = true,
            .has_colocation  = false,
            .has_bvh         = has_bvh_backend
        });
    }

    if (!has_bvh_backend) {
        return;
    }

    // Wrong BVH library tag: no build matches the running backend, so the
    // BVH is rejected; the marker and the topology stay in use.
    {
        Glb_chunks chunks = original;
        const std::string key = "\"library\":\"";
        const std::size_t position = chunks.json.find(key);
        ASSERT_NE(position, std::string::npos);
        const char digit = chunks.json[position + key.size()];
        replace_after(chunks.json, key, 1, (digit == '0') ? "1" : "0");
        expect_one_rejected(join_glb(chunks), "tampered_library.glb", Tamper_expectation{
            .reason          = "ERHE_raytrace_bvh: no build for the running backend and library",
            .marker_accepted = true,
            .has_colocation  = true,
            .has_bvh         = false
        });
    }

    // Truncated BVH nodes view.
    {
        Glb_chunks chunks = original;
        const uint64_t view_index = first_view_index(chunks.json, "nodes");
        simdjson::dom::parser  parser;
        const simdjson::dom::element root = parser.parse(chunks.json);
        const simdjson::dom::element view = root["bufferViews"].get_array().at(view_index);
        const uint64_t byte_offset = view["byteOffset"].get_uint64().value();
        const uint64_t byte_length = view["byteLength"].get_uint64().value();
        const std::string old_text = "\"byteLength\":" + std::to_string(byte_length) + ",\"byteOffset\":" + std::to_string(byte_offset);
        const std::string new_text = "\"byteLength\":" + std::to_string(byte_length - 28) + ",\"byteOffset\":" + std::to_string(byte_offset);
        const std::size_t position = chunks.json.find(old_text);
        ASSERT_NE(position, std::string::npos) << old_text;
        chunks.json.replace(position, old_text.size(), new_text);
        expect_one_rejected(join_glb(chunks), "tampered_nodes.glb", Tamper_expectation{
            .reason          = "ERHE_raytrace_bvh: nodes bufferView byteLength mismatch",
            .marker_accepted = true,
            .has_colocation  = true,
            .has_bvh         = false
        });
    }
}

} // anonymous namespace
