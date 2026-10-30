// The asset-root ERHE_pvs extension (doc/gltf_extensions/ERHE_pvs.md):
// export_gltf writes a ready Pvs_database, parse_gltf reads it back field by
// field and bit for bit, with the bits' byte range in the file recorded.

#include "gltf_test_util.hpp"

#include "erhe_item/item.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/pvs_database.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <simdjson.h>

#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

using erhe_gltf_test::parse_file;
using erhe_gltf_test::write_temporary_file;

[[nodiscard]] auto make_node(erhe::scene::Scene& scene, const std::string& name, const std::string& uid, const glm::vec3& position) -> std::shared_ptr<erhe::scene::Xform>
{
    const std::shared_ptr<erhe::scene::Xform> node = std::make_shared<erhe::scene::Xform>(name);
    node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    node->set_gltf_uid(uid);
    node->set_parent(scene.get_root_node());
    node->set_parent_from_node(erhe::math::create_translation<float>(position));
    return node;
}

[[nodiscard]] auto make_column(const std::string& uid, const uint32_t primitive_index, const float x) -> erhe::scene::Pvs_column
{
    erhe::scene::Pvs_column column{};
    column.node_uid        = uid;
    column.primitive_index = primitive_index;
    column.world_transform = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, x, 0.1f, -0.3f};
    column.world_bounds    = {x - 0.5f, -0.4f, -0.8f, x + 0.5f, 0.6f, 0.2f};
    column.transform_hash  = erhe::scene::compute_pvs_transform_hash(column.world_transform, column.world_bounds);
    return column;
}

// 5 x 2 x 2 cells, page size 4 -> 2 x 1 x 1 pages: page 1 (cells x = 4)
// not stored; page 0 stored, its cell 1 (x 1, y 0, z 0) without a PVS
// (all-ones words, mask bit clear). 3 columns, 1 word per cell.
[[nodiscard]] auto make_database(const erhe::scene::Pvs_state state) -> std::shared_ptr<erhe::scene::Pvs_database>
{
    erhe::scene::Pvs_header header{};
    header.scene_id              = "pvs \"test\" scene";
    header.grid_origin           = glm::vec3{-2.5f, 0.1f, -1.0f / 3.0f};
    header.cell_size             = 1.1f;
    header.cell_counts           = glm::uvec3{5u, 2u, 2u};
    header.page_size             = 4;
    header.bake_parameters.method           = erhe::scene::Pvs_bake_method::sampled;
    header.bake_parameters.face_resolution  = 512;
    header.bake_parameters.tile_size        = 4;
    header.bake_parameters.layer_count      = 32;
    header.bake_parameters.face_fov         = 1.9f;
    header.bake_parameters.samples_per_face = 2;
    header.bake_parameters.min_entry_pixels = 1.5f;
    header.bake_duration_seconds = 12.345f;

    std::vector<erhe::scene::Pvs_column> columns{
        make_column("uid_a", 0, 0.7f),
        make_column("uid_b", 0, -1.3f),
        make_column("uid_b", 1, 2.9f)
    };
    std::vector<erhe::scene::Pvs_page_entry> pages(2);
    std::vector<uint32_t> bits;
    pages[0].word_offset = 0;
    for (uint32_t local = 0; local < 64; ++local) {
        const uint32_t lx = local % 4;
        const uint32_t ly = (local / 4) % 4;
        const uint32_t lz = local / 16;
        const bool inside = (ly < 2) && (lz < 2);
        if (!inside || ((lx == 1) && (ly == 0) && (lz == 0))) {
            bits.push_back(0xffffffffu);
            continue;
        }
        pages[0].cells_with_pvs |= (uint64_t{1} << local);
        bits.push_back((lx + ly + lz) % 8u); // a different subset of the 3 columns per cell
    }
    EXPECT_EQ(erhe::scene::Pvs_database::validate(header, columns, pages, bits), std::string{});
    std::shared_ptr<erhe::scene::Pvs_database> database = std::make_shared<erhe::scene::Pvs_database>(
        std::move(header), std::move(columns), std::move(pages), std::move(bits)
    );
    if (state == erhe::scene::Pvs_state::stale) {
        database->mark_stale();
    }
    return database;
}

[[nodiscard]] auto export_with(const erhe::scene::Scene& scene, const std::shared_ptr<const erhe::scene::Pvs_database>& database) -> std::string
{
    erhe::gltf::Gltf_export_arguments arguments{
        .root_node = *scene.get_root_node(),
        .binary    = true
    };
    arguments.pvs_database = database;
    return erhe::gltf::export_gltf(arguments);
}

[[nodiscard]] auto glb_json(const std::string& glb) -> std::string
{
    EXPECT_GE(glb.size(), 20u);
    uint32_t json_length = 0;
    std::memcpy(&json_length, glb.data() + 12, sizeof(uint32_t));
    EXPECT_LE(20u + json_length, glb.size());
    return glb.substr(20, json_length);
}

[[nodiscard]] auto declares_pvs(const std::string& glb) -> bool
{
    return glb_json(glb).find("ERHE_pvs") != std::string::npos;
}

} // anonymous namespace

TEST(Gltf_pvs, round_trip_is_exact)
{
    erhe::scene::Scene scene{"source", nullptr};
    static_cast<void>(make_node(scene, "A",     "uid_a", glm::vec3{0.7f, 0.0f, 0.0f}));
    static_cast<void>(make_node(scene, "B",     "uid_b", glm::vec3{-1.3f, 0.0f, 0.0f}));
    static_cast<void>(make_node(scene, "Other", "uid_c", glm::vec3{0.0f, 5.0f, 0.0f}));
    const std::shared_ptr<erhe::scene::Pvs_database> source = make_database(erhe::scene::Pvs_state::ready);

    const std::string glb = export_with(scene, source);
    ASSERT_FALSE(glb.empty());

    // The JSON: version, page size, hexfloat header floats, identities by
    // node index, extensionsUsed only.
    const std::string json_text = glb_json(glb);
    simdjson::dom::parser  json_parser;
    simdjson::dom::element root;
    ASSERT_EQ(json_parser.parse(json_text).get(root), simdjson::SUCCESS);
    simdjson::dom::element extension;
    ASSERT_EQ(root["extensions"]["ERHE_pvs"].get(extension), simdjson::SUCCESS);
    EXPECT_EQ(extension["version"].get_uint64().value(), 1u);
    EXPECT_EQ(extension["page_size"].get_uint64().value(), 4u);
    EXPECT_EQ(extension["column_count"].get_uint64().value(), 3u);
    EXPECT_EQ(std::string_view{extension["cell_size"].get_string().value()}, std::string_view{"0x1.19999ap+0"});
    EXPECT_EQ(extension["column_identities"].get_array().size(), 3u);
    bool used = false;
    for (const simdjson::dom::element name : root["extensionsUsed"].get_array()) {
        used = used || (std::string_view{name.get_string().value()} == "ERHE_pvs");
    }
    EXPECT_TRUE(used);
    simdjson::dom::array required;
    if (root["extensionsRequired"].get_array().get(required) == simdjson::SUCCESS) {
        for (const simdjson::dom::element name : required) {
            EXPECT_NE(std::string_view{name.get_string().value()}, "ERHE_pvs");
        }
    }

    const std::filesystem::path path = write_temporary_file("pvs_round_trip.glb", glb);
    erhe::scene::Scene loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(path, loaded);
    ASSERT_TRUE(data.pvs_database);
    const erhe::scene::Pvs_database& result = *data.pvs_database;
    EXPECT_EQ(result.get_state(), erhe::scene::Pvs_state::ready);

    const erhe::scene::Pvs_header& expected_header = source->get_header();
    const erhe::scene::Pvs_header& header          = result.get_header();
    EXPECT_EQ(header.version,               expected_header.version);
    EXPECT_EQ(header.scene_id,              expected_header.scene_id);
    EXPECT_EQ(std::memcmp(&header.grid_origin, &expected_header.grid_origin, sizeof(glm::vec3)), 0);
    EXPECT_EQ(std::memcmp(&header.cell_size,   &expected_header.cell_size,   sizeof(float)), 0);
    EXPECT_EQ(header.cell_counts,           expected_header.cell_counts);
    EXPECT_EQ(header.page_size,             expected_header.page_size);
    EXPECT_EQ(header.bake_parameters.method,           expected_header.bake_parameters.method);
    EXPECT_EQ(header.bake_parameters.face_resolution,  expected_header.bake_parameters.face_resolution);
    EXPECT_EQ(header.bake_parameters.tile_size,        expected_header.bake_parameters.tile_size);
    EXPECT_EQ(header.bake_parameters.layer_count,      expected_header.bake_parameters.layer_count);
    EXPECT_EQ(header.bake_parameters.samples_per_face, expected_header.bake_parameters.samples_per_face);
    EXPECT_EQ(std::memcmp(&header.bake_parameters.face_fov,         &expected_header.bake_parameters.face_fov,         sizeof(float)), 0);
    EXPECT_EQ(std::memcmp(&header.bake_parameters.min_entry_pixels, &expected_header.bake_parameters.min_entry_pixels, sizeof(float)), 0);
    EXPECT_EQ(std::memcmp(&header.bake_duration_seconds,            &expected_header.bake_duration_seconds,            sizeof(float)), 0);

    ASSERT_EQ(result.get_column_count(), 3u);
    for (uint32_t i = 0; i < 3; ++i) {
        const erhe::scene::Pvs_column& expected = source->get_columns()[i];
        const erhe::scene::Pvs_column& column   = result.get_columns()[i];
        EXPECT_EQ(column.node_uid,        expected.node_uid);
        EXPECT_EQ(column.primitive_index, expected.primitive_index);
        EXPECT_EQ(std::memcmp(column.world_transform.data(), expected.world_transform.data(), sizeof(column.world_transform)), 0);
        EXPECT_EQ(std::memcmp(column.world_bounds.data(),    expected.world_bounds.data(),    sizeof(column.world_bounds)), 0);
        EXPECT_EQ(column.transform_hash,  expected.transform_hash);
        EXPECT_EQ(result.find_column(expected.node_uid, expected.primitive_index), i);
    }

    ASSERT_EQ(result.get_pages().size(), source->get_pages().size());
    EXPECT_EQ(std::memcmp(result.get_pages().data(), source->get_pages().data(), source->get_pages().size() * sizeof(erhe::scene::Pvs_page_entry)), 0);
    ASSERT_EQ(result.get_bits().size(), source->get_bits().size());
    EXPECT_EQ(std::memcmp(result.get_bits().data(), source->get_bits().data(), source->get_bits().size() * sizeof(uint32_t)), 0);
    EXPECT_EQ(result.get_stored_page_count(), 1u);
    EXPECT_FALSE(result.has_cell_pvs(1));
    EXPECT_TRUE (result.has_cell_pvs(0));
    EXPECT_FALSE(result.has_cell_pvs(4));

    // The bits' byte range in the file holds the bits.
    ASSERT_TRUE(result.get_bits_source().has_value());
    const erhe::scene::Pvs_bits_source& bits_source = result.get_bits_source().value();
    EXPECT_EQ(bits_source.path, path);
    ASSERT_EQ(bits_source.byte_length, source->get_bits().size() * sizeof(uint32_t));
    std::vector<uint32_t> file_bits(source->get_bits().size());
    std::ifstream file{bits_source.path, std::ios::binary};
    file.seekg(static_cast<std::streamoff>(bits_source.byte_offset));
    file.read(reinterpret_cast<char*>(file_bits.data()), static_cast<std::streamsize>(bits_source.byte_length));
    ASSERT_TRUE(static_cast<bool>(file));
    EXPECT_EQ(file_bits, source->get_bits());
}

TEST(Gltf_pvs, stale_database_is_not_written)
{
    erhe::scene::Scene scene{"source", nullptr};
    static_cast<void>(make_node(scene, "A", "uid_a", glm::vec3{0.7f, 0.0f, 0.0f}));
    static_cast<void>(make_node(scene, "B", "uid_b", glm::vec3{-1.3f, 0.0f, 0.0f}));
    const std::string glb = export_with(scene, make_database(erhe::scene::Pvs_state::stale));
    ASSERT_FALSE(glb.empty());
    EXPECT_FALSE(declares_pvs(glb));
}

TEST(Gltf_pvs, column_outside_the_export_skips_the_extension)
{
    erhe::scene::Scene scene{"source", nullptr};
    static_cast<void>(make_node(scene, "A", "uid_a", glm::vec3{0.7f, 0.0f, 0.0f}));
    const std::string glb = export_with(scene, make_database(erhe::scene::Pvs_state::ready));
    ASSERT_FALSE(glb.empty());
    EXPECT_FALSE(declares_pvs(glb));

    erhe::scene::Scene loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("pvs_missing_node.glb", glb), loaded);
    EXPECT_FALSE(data.pvs_database);
}

TEST(Gltf_pvs, no_database_no_extension)
{
    erhe::scene::Scene scene{"source", nullptr};
    static_cast<void>(make_node(scene, "A", "uid_a", glm::vec3{0.7f, 0.0f, 0.0f}));
    const std::string glb = export_with(scene, {});
    ASSERT_FALSE(glb.empty());
    EXPECT_FALSE(declares_pvs(glb));
    erhe::scene::Scene loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("pvs_none.glb", glb), loaded);
    EXPECT_FALSE(data.pvs_database);
}
