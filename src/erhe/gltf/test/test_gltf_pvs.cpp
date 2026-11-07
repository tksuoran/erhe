// The asset-root ERHE_pvs extension (doc/erhe/gltf.md "PVS items"):
// export_gltf writes it against a PVS item, and a parse of the document as an
// item (a Scene item) reads it back field by field and bit for bit through the
// item. A glTF file carries no PVS: a file parse reads none (R12.2).

#include "gltf_test_util.hpp"

#include "erhe_item/item.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/pvs_database.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <taskflow/taskflow.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using erhe_gltf_test::make_glb;
using erhe_gltf_test::parse_file;
using erhe_gltf_test::split_glb;
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
    header.bake_parameters.silhouette_tests = true;
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

class Memory_reader : public erhe::gltf::Gltf_buffer_reader
{
public:
    explicit Memory_reader(std::vector<std::byte> bytes) : m_bytes{std::move(bytes)} {}
    auto read_at(const std::uint64_t offset, const std::span<std::byte> out) const -> bool override
    {
        if ((offset > m_bytes.size()) || (out.size() > (m_bytes.size() - offset))) {
            return false;
        }
        std::memcpy(out.data(), m_bytes.data() + offset, out.size());
        return true;
    }
    std::vector<std::byte> m_bytes;
};

constexpr const char* c_pvs_item_uri = "erhe-item:00112233445566778899aabbccddeeff";

[[nodiscard]] auto export_with_item(const erhe::scene::Scene& scene, const erhe::gltf::Gltf_pvs_item& item) -> std::string
{
    erhe::gltf::Gltf_export_arguments arguments{
        .root_node = *scene.get_root_node(),
        .binary    = true
    };
    arguments.pvs_item = erhe::gltf::Gltf_item_reference{.uri = c_pvs_item_uri, .meta = item.meta};
    return erhe::gltf::export_gltf(arguments);
}

[[nodiscard]] auto parse_with_item(const std::filesystem::path& path, erhe::scene::Scene& scene, const erhe::gltf::Gltf_pvs_item& item) -> erhe::gltf::Gltf_data
{
    tf::Executor executor{1};
    const std::shared_ptr<Memory_reader> reader = std::make_shared<Memory_reader>(item.payload);
    return erhe::gltf::parse_gltf(
        erhe::gltf::Gltf_parse_arguments{
            .executor        = executor,
            .root_node       = scene.get_root_node(),
            .path            = path,
            .parallel        = false,
            .document        = erhe::gltf::Gltf_document::item,
            .buffer_resolver = [reader](const std::string_view uri) -> std::shared_ptr<const erhe::gltf::Gltf_buffer_reader> {
                return (uri == c_pvs_item_uri) ? reader : std::shared_ptr<const erhe::gltf::Gltf_buffer_reader>{};
            }
        }
    );
}

// A document carrying the PVS bytes in its BIN chunk (as prepared files of
// earlier builds did): the document written against the item, the item's
// bytes as its BIN chunk.
[[nodiscard]] auto write_inline_pvs_document(const erhe::scene::Scene& scene, const erhe::scene::Pvs_database& database) -> std::string
{
    const std::optional<erhe::gltf::Gltf_pvs_item> item = erhe::gltf::write_pvs_item(database);
    EXPECT_TRUE(item.has_value());
    if (!item.has_value()) {
        return {};
    }
    erhe_gltf_test::Glb_chunks chunks = split_glb(export_with_item(scene, item.value()));
    EXPECT_TRUE(chunks.bin.empty()) << "the item's buffer is the document's only buffer";
    // The buffer without its uri is the GLB's BIN chunk.
    const std::string uri_value = std::string{"\"uri\":\""} + c_pvs_item_uri + "\"";
    std::size_t       position  = chunks.json.find(uri_value + ",");
    std::size_t       length    = uri_value.size() + 1;
    if (position == std::string::npos) {
        position = chunks.json.find("," + uri_value);
        length   = uri_value.size() + 1;
    }
    EXPECT_NE(position, std::string::npos) << chunks.json;
    if (position == std::string::npos) {
        return {};
    }
    chunks.json.erase(position, length);
    const std::string_view payload{reinterpret_cast<const char*>(item->payload.data()), item->payload.size()};
    return make_glb(chunks.json, payload);
}

} // anonymous namespace

// A PVS item (doc/erhe/gltf.md "PVS items"): a document written against it
// carries no PVS bytes, and its parse through the item reads the database
// back bit for bit, with the columns resolved to the document's nodes.
TEST(Gltf_pvs, item_round_trip_is_exact)
{
    erhe::scene::Scene scene{"source", nullptr};
    static_cast<void>(make_node(scene, "Other", "uid_c", glm::vec3{0.0f, 5.0f, 0.0f}));
    static_cast<void>(make_node(scene, "B",     "uid_b", glm::vec3{-1.3f, 0.0f, 0.0f}));
    static_cast<void>(make_node(scene, "A",     "uid_a", glm::vec3{0.7f, 0.0f, 0.0f}));
    const std::shared_ptr<erhe::scene::Pvs_database> source = make_database(erhe::scene::Pvs_state::ready);
    const std::optional<erhe::gltf::Gltf_pvs_item> item = erhe::gltf::write_pvs_item(*source);
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(item->payload.size(), (3u * 80u) + (2u * 16u) + (source->get_bits().size() * 4u));
    EXPECT_NE(item->meta.find("\"uid_b\""), std::string::npos) << "columns are named by node uid";

    const std::string glb = export_with_item(scene, item.value());
    ASSERT_FALSE(glb.empty());
    EXPECT_TRUE(declares_pvs(glb));
    EXPECT_NE(glb_json(glb).find(c_pvs_item_uri), std::string::npos);
    EXPECT_LT(glb.size(), 4096u) << "no PVS bytes inside the document";

    erhe::scene::Scene loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = parse_with_item(write_temporary_file("pvs_item.glb", glb), loaded, item.value());
    ASSERT_TRUE(data.pvs_database);
    const erhe::scene::Pvs_database& result = *data.pvs_database;
    EXPECT_EQ(result.get_state(), erhe::scene::Pvs_state::ready);
    EXPECT_EQ(result.get_header().scene_id, source->get_header().scene_id);
    EXPECT_EQ(result.get_header().cell_counts, source->get_header().cell_counts);
    ASSERT_EQ(result.get_column_count(), 3u);
    for (uint32_t i = 0; i < 3; ++i) {
        const erhe::scene::Pvs_column& expected = source->get_columns()[i];
        const erhe::scene::Pvs_column& column   = result.get_columns()[i];
        EXPECT_EQ(column.node_uid,        expected.node_uid);
        EXPECT_EQ(column.primitive_index, expected.primitive_index);
        EXPECT_EQ(std::memcmp(column.world_transform.data(), expected.world_transform.data(), sizeof(column.world_transform)), 0);
        EXPECT_EQ(column.transform_hash,  expected.transform_hash);
    }
    EXPECT_EQ(result.get_pages().size(), source->get_pages().size());
    EXPECT_EQ(result.get_bits(), source->get_bits());
}

TEST(Gltf_pvs, item_column_outside_the_export_skips_the_extension)
{
    erhe::scene::Scene scene{"source", nullptr};
    static_cast<void>(make_node(scene, "A", "uid_a", glm::vec3{0.7f, 0.0f, 0.0f}));
    const std::optional<erhe::gltf::Gltf_pvs_item> item = erhe::gltf::write_pvs_item(*make_database(erhe::scene::Pvs_state::ready));
    ASSERT_TRUE(item.has_value());
    const std::string glb = export_with_item(scene, item.value());
    ASSERT_FALSE(glb.empty());
    EXPECT_FALSE(declares_pvs(glb));
}

TEST(Gltf_pvs, stale_database_makes_no_item)
{
    EXPECT_FALSE(erhe::gltf::write_pvs_item(*make_database(erhe::scene::Pvs_state::stale)).has_value());
}

TEST(Gltf_pvs, a_file_parse_reads_no_pvs)
{
    erhe::scene::Scene scene{"source", nullptr};
    static_cast<void>(make_node(scene, "A", "uid_a", glm::vec3{0.7f, 0.0f, 0.0f}));
    static_cast<void>(make_node(scene, "B", "uid_b", glm::vec3{-1.3f, 0.0f, 0.0f}));
    const std::string glb = write_inline_pvs_document(scene, *make_database(erhe::scene::Pvs_state::ready));
    ASSERT_FALSE(glb.empty());
    EXPECT_TRUE(declares_pvs(glb));

    erhe::scene::Scene loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("pvs_inline.glb", glb), loaded);
    EXPECT_FALSE(data.pvs_database) << "a glTF file carries no PVS";
    EXPECT_EQ(data.nodes.size(), 2u);
}

TEST(Gltf_pvs, no_database_no_extension)
{
    erhe::scene::Scene scene{"source", nullptr};
    static_cast<void>(make_node(scene, "A", "uid_a", glm::vec3{0.7f, 0.0f, 0.0f}));
    const std::string glb = erhe::gltf::export_gltf(*scene.get_root_node(), true);
    ASSERT_FALSE(glb.empty());
    EXPECT_FALSE(declares_pvs(glb));
    erhe::scene::Scene loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("pvs_none.glb", glb), loaded);
    EXPECT_FALSE(data.pvs_database);
}
