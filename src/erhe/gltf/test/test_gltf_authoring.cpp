// A scene file of authoring data alone (doc/erhe/gltf.md "Authoring data"):
// a raw export writes soups and LOD requests and no derived-data extension, a
// core loaded from its mesh item writes its authoring soup, and the derived
// data of files of earlier builds still reads.

#include "gltf_test_util.hpp"

#include "erhe_hash/content_hash.hpp"
#include "erhe_primitive/lod.hpp"
#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <gtest/gtest.h>

#include <taskflow/taskflow.hpp>

#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using erhe_gltf_test::data_path;
using erhe_gltf_test::write_temporary_file;

[[nodiscard]] auto parse(
    const std::filesystem::path&               path,
    erhe::scene::Scene&                        scene,
    const erhe::gltf::Gltf_document            document = erhe::gltf::Gltf_document::item
) -> erhe::gltf::Gltf_data
{
    tf::Executor executor{1};
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    return erhe::gltf::parse_gltf(
        erhe::gltf::Gltf_parse_arguments{
            .executor               = executor,
            .root_node              = root_node,
            .path                   = path,
            .parallel               = false,
            .document               = document,
            .content_hashes         = erhe::gltf::Gltf_content_hash_load::compute,
            .triangle_soup_reload   = erhe::gltf::Triangle_soup_reload::record
        }
    );
}

[[nodiscard]] auto get_primitives(const erhe::gltf::Gltf_data& data) -> std::vector<std::shared_ptr<erhe::primitive::Primitive>>
{
    std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives;
    for (const std::shared_ptr<erhe::scene::Node>& node : data.nodes) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
        if (!mesh) {
            continue;
        }
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (mesh_primitive.primitive && mesh_primitive.primitive->render_shape) {
                primitives.push_back(mesh_primitive.primitive);
            }
        }
    }
    return primitives;
}

[[nodiscard]] auto export_scene(const erhe::scene::Scene& scene, const erhe::gltf::Prepared_mesh_data prepared_mesh_data) -> std::string
{
    const erhe::gltf::Gltf_export_arguments arguments{
        .root_node          = *scene.get_root_node(),
        .binary             = true,
        .prepared_mesh_data = prepared_mesh_data
    };
    return erhe::gltf::export_gltf(arguments);
}

// The JSON chunk of a GLB.
[[nodiscard]] auto get_json(const std::string& glb) -> std::string
{
    if (glb.size() < 20) {
        return {};
    }
    std::uint32_t length = 0;
    std::memcpy(&length, glb.data() + 12, sizeof(length));
    return glb.substr(20, length);
}

[[nodiscard]] auto make_request() -> erhe::primitive::Lod_settings
{
    erhe::primitive::Lod_settings settings{};
    settings.level_count = 3;
    settings.ratio       = 0.5f;
    return settings;
}

class Bytes_reader : public erhe::gltf::Gltf_buffer_reader
{
public:
    explicit Bytes_reader(std::vector<std::byte> bytes) : m_bytes{std::move(bytes)} {}
    auto read_at(const std::uint64_t offset, const std::span<std::byte> out) const -> bool override
    {
        if ((offset > m_bytes.size()) || (out.size() > (m_bytes.size() - offset))) {
            return false;
        }
        std::memcpy(out.data(), m_bytes.data() + offset, out.size());
        return true;
    }
private:
    std::vector<std::byte> m_bytes;
};

} // anonymous namespace

TEST(Gltf_authoring, a_raw_export_writes_no_derived_data_and_keeps_the_lod_request)
{
    erhe::scene::Scene source_scene{"source", nullptr};
    const erhe::gltf::Gltf_data source = parse(data_path("variants.gltf"), source_scene);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives = get_primitives(source);
    ASSERT_FALSE(primitives.empty());
    primitives.front()->lod_settings = make_request();
    std::vector<erhe::hash::Content_hash> source_hashes;
    for (const std::shared_ptr<erhe::primitive::Primitive>& primitive : primitives) {
        source_hashes.push_back(primitive->render_shape->get_content_hash().value());
    }

    const std::string saved = export_scene(source_scene, erhe::gltf::Prepared_mesh_data::none);
    ASSERT_FALSE(saved.empty());
    const std::string json = get_json(saved);
    for (const char* const name : {"ERHE_mesh_optimized", "ERHE_mesh_topology", "ERHE_raytrace_bvh", "ERHE_gpu_vertex_streams", "source_content_hash", "\"levels\""}) {
        EXPECT_EQ(json.find(name), std::string::npos) << name;
    }
    EXPECT_NE(json.find("\"ERHE_mesh_lod\":{\"version\":1,\"settings\":{"), std::string::npos);

    erhe::scene::Scene reopened_scene{"reopened", nullptr};
    const erhe::gltf::Gltf_data reopened = parse(write_temporary_file("authoring_raw.glb", saved), reopened_scene);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> reopened_primitives = get_primitives(reopened);
    ASSERT_EQ(reopened_primitives.size(), primitives.size());
    for (std::size_t i = 0, end = reopened_primitives.size(); i < end; ++i) {
        EXPECT_FALSE(reopened_primitives[i]->is_prepared());
        // The file holds the soups: the reopen keys them as the source.
        EXPECT_EQ(reopened_primitives[i]->render_shape->get_content_hash().value(), source_hashes[i]);
    }
    ASSERT_TRUE(reopened_primitives.front()->lod_settings.has_value());
    EXPECT_EQ(reopened_primitives.front()->lod_settings->level_count, 3u);
    EXPECT_EQ(reopened_primitives.front()->lod_settings->ratio, 0.5f);
}

// A core loaded from its mesh item in place of the source soup: a raw export
// writes the source soup, so the saved file keys as the source (R6.1).
TEST(Gltf_authoring, a_core_loaded_from_its_item_writes_its_authoring_soup)
{
    erhe::scene::Scene source_scene{"source", nullptr};
    const erhe::gltf::Gltf_data source = parse(data_path("variants.gltf"), source_scene);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives = get_primitives(source);
    ASSERT_FALSE(primitives.empty());
    const std::shared_ptr<erhe::primitive::Primitive>& primitive = primitives.front();
    const std::shared_ptr<erhe::primitive::Primitive_render_shape> source_shape = primitive->render_shape;
    const erhe::hash::Content_hash source_hash = source_shape->get_content_hash().value();

    ASSERT_TRUE(source_shape->make_geometry());
    erhe::primitive::Prepare_primitive_result prepared = erhe::primitive::prepare_primitive_for_export(*source_shape);
    ASSERT_TRUE(prepared.prepared.has_value());
    erhe::gltf::Gltf_mesh_item item = erhe::gltf::write_mesh_item(prepared.prepared.value(), nullptr);
    const std::size_t core_vertex_count = erhe::primitive::get_export_core(prepared.prepared.value()).get_vertex_count();

    tf::Executor executor{1};
    const std::shared_ptr<Bytes_reader> reader = std::make_shared<Bytes_reader>(std::move(item.payload));
    std::string error;
    const std::shared_ptr<erhe::primitive::Primitive> item_primitive = erhe::gltf::load_mesh_item_primitive(
        erhe::gltf::Gltf_mesh_item_load_arguments{
            .executor        = executor,
            .reference       = erhe::gltf::Gltf_mesh_item_reference{.uri = "erhe-item:test", .meta = item.meta},
            .buffer_resolver = [reader](const std::string_view uri) -> std::shared_ptr<const erhe::gltf::Gltf_buffer_reader> {
                return (uri == "erhe-item:test") ? reader : std::shared_ptr<const erhe::gltf::Gltf_buffer_reader>{};
            },
            .path            = "memory"
        },
        error
    );
    ASSERT_TRUE(item_primitive) << error;
    ASSERT_TRUE(item_primitive->is_prepared());
    const std::shared_ptr<erhe::primitive::Primitive_render_shape> item_shape = item_primitive->render_shape;
    EXPECT_EQ(item_shape->get_triangle_soup()->get_vertex_count(), core_vertex_count);
    EXPECT_FALSE(item_shape->has_authoring_soup());

    item_shape->set_content_hash(source_hash);
    // The fixture's buffers are data uris: the soup has no source, the authoring soup is resident.
    item_shape->set_authoring_soup(source_shape->get_triangle_soup(), source_shape->get_triangle_soup_source());
    ASSERT_TRUE(item_shape->has_authoring_soup());
    primitive->render_shape = item_shape;

    const std::string saved = export_scene(source_scene, erhe::gltf::Prepared_mesh_data::none);
    ASSERT_FALSE(saved.empty());
    EXPECT_EQ(get_json(saved).find("ERHE_mesh_optimized"), std::string::npos);
    erhe::scene::Scene reopened_scene{"reopened", nullptr};
    const erhe::gltf::Gltf_data reopened = parse(write_temporary_file("authoring_item.glb", saved), reopened_scene);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> reopened_primitives = get_primitives(reopened);
    ASSERT_FALSE(reopened_primitives.empty());
    EXPECT_EQ(reopened_primitives.front()->render_shape->get_content_hash().value(), source_hash);
    EXPECT_EQ(
        erhe::primitive::compute_triangle_soup_content_hash(*reopened_primitives.front()->render_shape->get_triangle_soup()),
        source_hash
    );

    // A prepared export (a Scene item) writes the core it holds, not the authoring soup.
    const std::string prepared_file = export_scene(source_scene, erhe::gltf::Prepared_mesh_data::write);
    EXPECT_NE(get_json(prepared_file).find("ERHE_mesh_optimized"), std::string::npos);
}

// A glTF file carrying the derived-data extensions (prepared cores with their
// chains, as earlier builds saved): a file parse reads none of them, its
// primitives are plain soups and its LOD request is read; the same document
// parsed as an item (a Scene item) reads its prepared data (R12.2).
TEST(Gltf_authoring, a_file_parse_reads_no_derived_data)
{
    erhe::scene::Scene source_scene{"source", nullptr};
    const erhe::gltf::Gltf_data source = parse(data_path("lod_grids.gltf"), source_scene);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives = get_primitives(source);
    ASSERT_FALSE(primitives.empty());
    primitives.front()->lod_settings = make_request();
    const erhe::gltf::Gltf_export_arguments arguments{
        .root_node          = *source_scene.get_root_node(),
        .binary             = true,
        .prepared_mesh_data = erhe::gltf::Prepared_mesh_data::write
    };
    const std::string document = erhe::gltf::export_gltf(arguments);
    ASSERT_FALSE(document.empty());
    ASSERT_NE(get_json(document).find("\"levels\""), std::string::npos);
    const std::filesystem::path path = write_temporary_file("authoring_derived.glb", document);

    erhe::scene::Scene item_scene{"item", nullptr};
    const erhe::gltf::Gltf_data item = parse(path, item_scene, erhe::gltf::Gltf_document::item);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> item_primitives = get_primitives(item);
    ASSERT_FALSE(item_primitives.empty());
    EXPECT_TRUE(item_primitives.front()->is_prepared());
    ASSERT_TRUE(item_primitives.front()->lod_settings.has_value());

    erhe::scene::Scene file_scene{"file", nullptr};
    const erhe::gltf::Gltf_data file = parse(path, file_scene, erhe::gltf::Gltf_document::file);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> file_primitives = get_primitives(file);
    ASSERT_FALSE(file_primitives.empty());
    for (const std::shared_ptr<erhe::primitive::Primitive>& primitive : file_primitives) {
        EXPECT_FALSE(primitive->is_prepared());
        EXPECT_TRUE(primitive->render_shape->get_triangle_soup_counts().index_count > 0);
    }
    EXPECT_EQ(file.prepared_mesh_statistics.used_count, 0u);
    EXPECT_GT(file.prepared_mesh_statistics.ignored_count, 0u);
    ASSERT_TRUE(file_primitives.front()->lod_settings.has_value());
    EXPECT_EQ(file_primitives.front()->lod_settings->level_count, 3u);
}
