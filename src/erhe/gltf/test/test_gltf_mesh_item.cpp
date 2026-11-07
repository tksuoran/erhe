// Mesh items (doc/erhe/gltf.md "Mesh items"): write_mesh_item(), an export
// that writes primitives against items through a provider, and a parse that
// reads the items' payloads through a resolver, with an in-memory store
// standing in for the editor's asset store.

#include "gltf_test_util.hpp"

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/lod.hpp"
#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/stored_vertex_streams.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_raytrace/prebuilt_bvh.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <gtest/gtest.h>

#include <simdjson.h>
#include <taskflow/taskflow.hpp>

#include <fmt/format.h>

#include <array>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

using erhe_gltf_test::data_path;
using erhe_gltf_test::make_test_gpu_formats;
using erhe_gltf_test::parse_file;
using erhe_gltf_test::write_temporary_file;

class Memory_item_reader : public erhe::gltf::Gltf_buffer_reader
{
public:
    explicit Memory_item_reader(std::vector<std::byte> bytes) : m_bytes{std::move(bytes)} {}

    auto read_at(const std::uint64_t offset, const std::span<std::byte> out) const -> bool override
    {
        {
            const std::lock_guard<std::mutex> lock{m_mutex};
            ++m_read_count;
        }
        if ((offset > m_bytes.size()) || (out.size() > (m_bytes.size() - offset))) {
            return false;
        }
        std::memcpy(out.data(), m_bytes.data() + offset, out.size());
        return true;
    }

    [[nodiscard]] auto get_read_count() const -> std::size_t
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        return m_read_count;
    }

private:
    std::vector<std::byte> m_bytes;
    mutable std::mutex     m_mutex;
    mutable std::size_t    m_read_count{0};
};

// The items by buffer uri, as the editor's asset store would hold them.
class Memory_item_store
{
public:
    std::map<std::string, std::shared_ptr<Memory_item_reader>> readers;
    std::map<std::string, std::string>                         metas;     // mesh items
    std::map<std::string, std::string>                         bvh_metas; // Raytrace BVH items

    [[nodiscard]] auto get_resolver() const -> erhe::gltf::Gltf_buffer_resolver
    {
        return [this](const std::string_view uri) -> std::shared_ptr<const erhe::gltf::Gltf_buffer_reader> {
            const auto i = readers.find(std::string{uri});
            return (i != readers.end()) ? i->second : std::shared_ptr<const erhe::gltf::Gltf_buffer_reader>{};
        };
    }

    [[nodiscard]] auto get_read_count() const -> std::size_t
    {
        std::size_t count = 0;
        for (const auto& [uri, reader] : readers) {
            count += reader->get_read_count();
        }
        return count;
    }
};

[[nodiscard]] auto find_node_primitive(const erhe::gltf::Gltf_data& data, const std::string_view name) -> std::shared_ptr<erhe::primitive::Primitive>
{
    for (const std::shared_ptr<erhe::scene::Node>& node : data.nodes) {
        if (node && (node->get_name() == name)) {
            const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
            if (mesh && !mesh->get_primitives().empty()) {
                return mesh->get_primitives().front().primitive;
            }
        }
    }
    return {};
}

const std::array<const char*, 2> c_node_names{"Chained", "Unchained"};

enum class Bvh_items : unsigned int
{
    referenced = 0,
    none
};

// lod_grids.gltf with a LOD chain requested on "Chained", the prepared form
// of each of its two primitives (chain and GPU streams included), and their
// mesh items in a store.
class Mesh_item_fixture
{
public:
    Mesh_item_fixture()
        : formats_pair{make_test_gpu_formats()}
        , formats{
            .base              = &formats_pair.first,
            .base_skinned      = &formats_pair.first,
            .optimized         = &formats_pair.second,
            .optimized_skinned = &formats_pair.second,
            .index_type        = erhe::dataformat::Format::format_32_scalar_uint,
            .encoding_version  = erhe::primitive::vertex_encoding_version
        }
    {
        source = parse_file(data_path("lod_grids.gltf"), source_scene);
        const std::shared_ptr<erhe::primitive::Primitive> chained = find_node_primitive(source, "Chained");
        EXPECT_TRUE(chained);
        if (chained) {
            chained->lod_settings = erhe::primitive::Lod_settings{};
        }
        for (std::size_t i = 0; i < c_node_names.size(); ++i) {
            const std::shared_ptr<erhe::primitive::Primitive> primitive = find_node_primitive(source, c_node_names[i]);
            EXPECT_TRUE(primitive && primitive->render_shape);
            if (!primitive || !primitive->render_shape) {
                continue;
            }
            erhe::primitive::Prepare_primitive_result result = erhe::primitive::prepare_primitive_for_export(*primitive->render_shape);
            EXPECT_TRUE(result.prepared.has_value()) << result.skip_reason;
            if (!result.prepared.has_value()) {
                continue;
            }
            if (primitive->lod_settings.has_value()) {
                static_cast<void>(erhe::primitive::make_export_lod_chain(result.prepared.value(), *primitive->render_shape, primitive->lod_settings.value()));
            }
            const erhe::gltf::Gltf_mesh_item item = erhe::gltf::write_mesh_item(result.prepared.value(), &formats);
            const std::string uri = fmt::format("erhe-item:{:032x}", i + 1);
            store.readers[uri] = std::make_shared<Memory_item_reader>(item.payload);
            store.metas  [uri] = item.meta;
            uri_by_shape[primitive->render_shape.get()] = uri;
            if (result.prepared->bvh.has_value()) {
                const erhe::gltf::Gltf_raytrace_bvh_item bvh_item = erhe::gltf::write_raytrace_bvh_item(result.prepared->bvh.value());
                const std::string bvh_uri = fmt::format("erhe-item:{:032x}", i + 101);
                store.readers  [bvh_uri] = std::make_shared<Memory_item_reader>(bvh_item.payload);
                store.bvh_metas[bvh_uri] = bvh_item.meta;
                bvh_uri_by_shape[primitive->render_shape.get()] = bvh_uri;
            }
            prepared.push_back(std::move(result.prepared.value()));
        }
    }

    // The scene as a prepared save writes it (the file form).
    [[nodiscard]] auto export_file_form() const -> std::string
    {
        erhe::gltf::Gltf_export_arguments arguments{
            .root_node          = *source_scene.get_root_node(),
            .binary             = true,
            .prepared_mesh_data = erhe::gltf::Prepared_mesh_data::write
        };
        arguments.gpu_vertex_formats = &formats;
        return erhe::gltf::export_gltf(arguments);
    }

    // The scene with every soup primitive written against its item, and its BVH item unless bvh_items is none.
    [[nodiscard]] auto export_item_form(erhe::gltf::Gltf_export_prepared_statistics* statistics, const Bvh_items bvh_items = Bvh_items::referenced) const -> std::string
    {
        erhe::gltf::Gltf_export_arguments arguments{
            .root_node          = *source_scene.get_root_node(),
            .binary             = true,
            .prepared_mesh_data = erhe::gltf::Prepared_mesh_data::write
        };
        arguments.gpu_vertex_formats  = &formats;
        arguments.prepared_statistics = statistics;
        arguments.mesh_item_provider  = [this, bvh_items](const erhe::primitive::Primitive_shape& shape) -> std::optional<erhe::gltf::Gltf_mesh_item_reference> {
            const auto i = uri_by_shape.find(&shape);
            if (i == uri_by_shape.end()) {
                return std::nullopt;
            }
            erhe::gltf::Gltf_mesh_item_reference reference{.uri = i->second, .meta = store.metas.at(i->second)};
            const auto bvh = bvh_uri_by_shape.find(&shape);
            if ((bvh_items == Bvh_items::referenced) && (bvh != bvh_uri_by_shape.end())) {
                reference.raytrace_bvh = erhe::gltf::Gltf_item_reference{.uri = bvh->second, .meta = store.bvh_metas.at(bvh->second)};
            }
            return reference;
        };
        return erhe::gltf::export_gltf(arguments);
    }

    std::pair<erhe::dataformat::Vertex_format, erhe::dataformat::Vertex_format> formats_pair;
    erhe::gltf::Gltf_gpu_vertex_formats                                          formats;
    erhe::scene::Scene                                                           source_scene{"source", nullptr};
    erhe::gltf::Gltf_data                                                        source;
    std::vector<erhe::primitive::Prepared_primitive_export>                      prepared; // parallel to c_node_names
    Memory_item_store                                                            store;
    std::map<const erhe::primitive::Primitive_shape*, std::string>               uri_by_shape;
    std::map<const erhe::primitive::Primitive_shape*, std::string>               bvh_uri_by_shape;
};

[[nodiscard]] auto parse(
    const std::filesystem::path&             path,
    erhe::scene::Scene&                      scene,
    const erhe::gltf::Gltf_buffer_resolver&  resolver,
    const erhe::gltf::Mesh_payload_load      mesh_payload,
    const erhe::gltf::Triangle_soup_reload   reload,
    const std::span<const std::byte>         glb_data = {},
    const erhe::gltf::Stored_bvh_use         stored_bvh_use = erhe::gltf::Stored_bvh_use::use
) -> erhe::gltf::Gltf_data
{
    tf::Executor executor{1};
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    return erhe::gltf::parse_gltf(
        erhe::gltf::Gltf_parse_arguments{
            .executor             = executor,
            .root_node            = root_node,
            .path                 = path,
            .parallel             = false,
            .glb_data             = glb_data,
            .document             = erhe::gltf::Gltf_document::item,
            .triangle_soup_reload = reload,
            .mesh_payload         = mesh_payload,
            .buffer_resolver      = resolver,
            .stored_bvh_use       = stored_bvh_use
        }
    );
}

[[nodiscard]] auto glb_json_chunk(const std::string& glb) -> std::string
{
    uint32_t json_length = 0;
    std::memcpy(&json_length, glb.data() + 12, 4);
    return glb.substr(20, json_length);
}

[[nodiscard]] auto glb_has_bin_chunk(const std::string& glb) -> bool
{
    uint32_t json_length = 0;
    std::memcpy(&json_length, glb.data() + 12, 4);
    return glb.size() > (std::size_t{20} + json_length);
}

void expect_same_bvh(const erhe::raytrace::Prebuilt_bvh& a, const erhe::raytrace::Prebuilt_bvh& b)
{
    EXPECT_EQ(a.triangle_hash,  b.triangle_hash);
    EXPECT_EQ(a.triangle_count, b.triangle_count);
    EXPECT_EQ(a.library,        b.library);
    ASSERT_EQ(a.nodes.size(),   b.nodes.size());
    EXPECT_EQ(std::memcmp(a.nodes.data(), b.nodes.data(), a.nodes.size() * sizeof(erhe::raytrace::Bvh_node_record)), 0);
    EXPECT_EQ(a.primitive_ids, b.primitive_ids);
}

// The prepared primitive's payloads read back through the item: the core,
// topology, LOD level indices, BVH and stored GPU streams, bit for bit.
TEST(Gltf_mesh_item, prepared_primitive_round_trips_bit_for_bit)
{
    Mesh_item_fixture fixture;
    ASSERT_EQ(fixture.prepared.size(), c_node_names.size());
    const std::string glb = fixture.export_item_form(nullptr);
    ASSERT_FALSE(glb.empty());
    const std::filesystem::path path = write_temporary_file("mesh_item_round_trip.glb", glb);

    erhe::scene::Scene    scene{"items", nullptr};
    erhe::gltf::Gltf_data data = parse(path, scene, fixture.store.get_resolver(), erhe::gltf::Mesh_payload_load::deferred, erhe::gltf::Triangle_soup_reload::none);
    ASSERT_TRUE(data.errors.empty());
    for (std::size_t i = 0; i < c_node_names.size(); ++i) {
        const erhe::primitive::Prepared_primitive_export& expected = fixture.prepared[i];
        const erhe::primitive::Triangle_soup&             core     = erhe::primitive::get_export_core(expected);
        const std::shared_ptr<erhe::primitive::Primitive> primitive = find_node_primitive(data, c_node_names[i]);
        ASSERT_TRUE(primitive && primitive->render_shape) << c_node_names[i];
        ASSERT_TRUE(primitive->render_shape->is_payload_pending());

        // GPU streams, read before the payload (the stored build's order).
        const std::shared_ptr<const erhe::primitive::Triangle_soup_source> source = primitive->render_shape->get_triangle_soup_source();
        ASSERT_TRUE(source);
        const erhe::primitive::Stored_vertex_stream_request request{
            .base_format      = &fixture.formats_pair.first,
            .optimized_format = &fixture.formats_pair.second,
            .index_type       = erhe::dataformat::Format::format_32_scalar_uint,
            .encoding_version = erhe::primitive::vertex_encoding_version
        };
        std::string reason;
        const std::unique_ptr<erhe::primitive::Stored_vertex_streams> stored = source->load_stored_vertex_streams(request, reason);
        ASSERT_TRUE(stored) << reason;
        EXPECT_EQ(stored->indices, std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(core.index_data.data()), reinterpret_cast<const uint8_t*>(core.index_data.data() + core.index_data.size())));
        const std::optional<erhe::primitive::Packed_vertex_streams> base      = erhe::primitive::pack_triangle_soup_streams(core, fixture.formats_pair.first,  erhe::dataformat::Format::format_32_scalar_uint);
        const std::optional<erhe::primitive::Packed_vertex_streams> optimized = erhe::primitive::pack_triangle_soup_streams(core, fixture.formats_pair.second, erhe::dataformat::Format::format_32_scalar_uint);
        ASSERT_TRUE(base.has_value() && optimized.has_value() && stored->optimized.has_value());
        EXPECT_EQ(stored->base.streams,       base->streams);
        EXPECT_EQ(stored->optimized->streams, optimized->streams);
        EXPECT_EQ(std::memcmp(&stored->position_quantization, &optimized->position_quantization, sizeof(optimized->position_quantization)), 0);
        EXPECT_EQ(std::memcmp(&stored->texcoord_quantization, &optimized->texcoord_quantization, sizeof(optimized->texcoord_quantization)), 0);
        EXPECT_EQ(stored->lod_indices, expected.lod.has_value() ? expected.lod->chain.indices : std::vector<uint32_t>{});

        // The core, topology, BVH and LOD chain through the soup load.
        ASSERT_TRUE(primitive->load_payload());
        ASSERT_TRUE(primitive->is_prepared());
        const std::shared_ptr<erhe::primitive::Triangle_soup> soup = primitive->render_shape->get_triangle_soup();
        ASSERT_TRUE(soup);
        EXPECT_EQ(soup->index_data, core.index_data);
        EXPECT_EQ(soup->get_vertex_count(), core.get_vertex_count());
        // The payload validation of the soup, as a load runs it.
        const std::shared_ptr<erhe::primitive::Prepared_primitive_data> prepared_data = source->load_prepared_data(*soup);
        ASSERT_TRUE(prepared_data);
        EXPECT_EQ(prepared_data->content_hash, expected.content_hash);
        ASSERT_TRUE(prepared_data->colocation.has_value());
        EXPECT_EQ(prepared_data->colocation->vertex_points, expected.vertex_points);
        EXPECT_EQ(prepared_data->bvh.has_value(), expected.bvh.has_value());
        if (prepared_data->bvh.has_value() && expected.bvh.has_value()) {
            expect_same_bvh(prepared_data->bvh.value(), expected.bvh.value());
        }
        EXPECT_EQ(prepared_data->lod.has_value(), expected.lod.has_value());
        if (expected.lod.has_value()) {
            EXPECT_EQ(soup->lod_index_data, expected.lod->chain.indices);
            ASSERT_TRUE(prepared_data->lod.has_value());
            EXPECT_EQ(prepared_data->lod->settings, expected.lod->settings);
            ASSERT_EQ(prepared_data->lod->chain.levels.size(), expected.lod->chain.levels.size());
            for (std::size_t level = 0; level < expected.lod->chain.levels.size(); ++level) {
                EXPECT_EQ(prepared_data->lod->chain.levels[level].index_count, expected.lod->chain.levels[level].index_count);
                EXPECT_EQ(std::memcmp(&prepared_data->lod->chain.levels[level].error, &expected.lod->chain.levels[level].error, sizeof(float)), 0);
            }
        }
    }
    EXPECT_TRUE(fixture.prepared[0].lod.has_value()); // the fixture exercises a chain
    EXPECT_EQ(fixture.prepared[0].bvh.has_value(), !erhe::raytrace::get_prebuilt_bvh_backend().empty());
    erhe::gltf::finish_prepared_mesh_statistics(data, path);
    erhe::gltf::close_source_files(data);
    EXPECT_EQ(data.prepared_mesh_statistics.rejected_count, 0u);
    EXPECT_GT(fixture.store.get_read_count(), 0u);
}

// An export with a provider: each primitive's accessors read a buffer named
// by the item's uri, the file holds no bytes of them (no BIN chunk: the
// fixture has no images), and nothing is prepared.
TEST(Gltf_mesh_item, export_with_provider_writes_no_primitive_bytes)
{
    Mesh_item_fixture fixture;
    erhe::gltf::Gltf_export_prepared_statistics statistics;
    const std::string glb = fixture.export_item_form(&statistics);
    ASSERT_FALSE(glb.empty());
    EXPECT_EQ(statistics.mesh_item_count, c_node_names.size());
    EXPECT_EQ(statistics.prepared_count,  0u);
    EXPECT_FALSE(glb_has_bin_chunk(glb));

    const std::string            json_text = glb_json_chunk(glb);
    simdjson::dom::parser        parser;
    const simdjson::dom::element root = parser.parse(json_text);
    std::vector<std::string> uris;
    for (const simdjson::dom::element buffer : root["buffers"].get_array()) {
        std::string_view uri;
        ASSERT_EQ(buffer["uri"].get_string().get(uri), simdjson::SUCCESS);
        EXPECT_TRUE(fixture.store.readers.contains(std::string{uri})) << uri;
        uris.emplace_back(uri);
    }
    EXPECT_EQ(uris.size(), c_node_names.size() + fixture.bvh_uri_by_shape.size());
    bool uses_lod = false;
    bool uses_streams = false;
    for (const simdjson::dom::element name : root["extensionsUsed"].get_array()) {
        uses_lod     = uses_lod     || (name.get_string().value() == "ERHE_mesh_lod");
        uses_streams = uses_streams || (name.get_string().value() == "ERHE_gpu_vertex_streams");
    }
    EXPECT_TRUE(uses_lod);
    EXPECT_TRUE(uses_streams);

    // The meta stands alone: its views lie in its payload.
    for (const auto& [uri, meta_text] : fixture.store.metas) {
        simdjson::dom::parser        meta_parser;
        const simdjson::dom::element meta = meta_parser.parse(meta_text);
        const uint64_t byte_length = meta["byteLength"].get_uint64().value();
        EXPECT_GT(byte_length, 0u) << uri;
        for (const simdjson::dom::element view : meta["bufferViews"].get_array()) {
            EXPECT_LE(view["byteOffset"].get_uint64().value() + view["byteLength"].get_uint64().value(), byte_length);
        }
        EXPECT_EQ(meta["primitive"]["extensions"]["ERHE_mesh_optimized"].error(), simdjson::SUCCESS);
        EXPECT_EQ(meta["primitive"]["extensions"]["ERHE_raytrace_bvh"].error(), simdjson::NO_SUCH_FIELD) << uri;
    }
}

// A BVH item: its meta names two views (nodes, primitive ids) in its own
// payload and the ERHE_raytrace_bvh extension over them; the export writes
// the extension against the BVH item's buffer.
TEST(Gltf_mesh_item, raytrace_bvh_item_is_its_own_buffer)
{
    Mesh_item_fixture fixture;
    if (erhe::raytrace::get_prebuilt_bvh_backend().empty()) {
        GTEST_SKIP() << "the raytrace backend exports no BVH";
    }
    ASSERT_EQ(fixture.bvh_uri_by_shape.size(), c_node_names.size());
    for (const auto& [uri, meta_text] : fixture.store.bvh_metas) {
        simdjson::dom::parser        meta_parser;
        const simdjson::dom::element meta = meta_parser.parse(meta_text);
        const uint64_t byte_length = meta["byteLength"].get_uint64().value();
        const simdjson::dom::array views = meta["bufferViews"].get_array();
        EXPECT_EQ(views.size(), 2u) << uri;
        for (const simdjson::dom::element view : views) {
            EXPECT_LE(view["byteOffset"].get_uint64().value() + view["byteLength"].get_uint64().value(), byte_length);
        }
        const simdjson::dom::element build = meta["extension"]["builds"].at(0);
        EXPECT_EQ(build["nodes"].get_uint64().value(), 0u) << uri;
        EXPECT_EQ(build["primitive_ids"].get_uint64().value(), 1u) << uri;
        EXPECT_EQ(std::string{build["library"].get_string().value()}, std::string{erhe::raytrace::get_prebuilt_bvh_library()});
    }

    const std::string glb = fixture.export_item_form(nullptr);
    ASSERT_FALSE(glb.empty());
    const std::string            json_text = glb_json_chunk(glb);
    simdjson::dom::parser        parser;
    const simdjson::dom::element root = parser.parse(json_text);
    const simdjson::dom::array   buffers = root["buffers"].get_array();
    const simdjson::dom::array   views   = root["bufferViews"].get_array();
    std::size_t bvh_primitive_count = 0;
    for (const simdjson::dom::element mesh : root["meshes"].get_array()) {
        for (const simdjson::dom::element primitive : mesh["primitives"].get_array()) {
            const simdjson::dom::element build = primitive["extensions"]["ERHE_raytrace_bvh"]["builds"].at(0);
            for (const char* const member : {"nodes", "primitive_ids"}) {
                const uint64_t view_index   = build[member].get_uint64().value();
                const uint64_t buffer_index = views.at(view_index)["buffer"].get_uint64().value();
                const std::string_view uri  = buffers.at(buffer_index)["uri"].get_string().value();
                EXPECT_TRUE(fixture.store.bvh_metas.contains(std::string{uri})) << uri;
            }
            ++bvh_primitive_count;
        }
    }
    EXPECT_EQ(bvh_primitive_count, c_node_names.size());
}

// Without a BVH reference the primitive has no ERHE_raytrace_bvh and loads
// without a stored BVH; Stored_bvh_use::ignore loads a referenced one the same way.
TEST(Gltf_mesh_item, primitive_without_bvh_item_loads_without_stored_bvh)
{
    Mesh_item_fixture fixture;
    const std::string with_bvh    = fixture.export_item_form(nullptr, Bvh_items::referenced);
    const std::string without_bvh = fixture.export_item_form(nullptr, Bvh_items::none);
    ASSERT_FALSE(with_bvh.empty());
    ASSERT_FALSE(without_bvh.empty());
    EXPECT_EQ(glb_json_chunk(without_bvh).find("ERHE_raytrace_bvh"), std::string::npos);
    const std::filesystem::path with_path    = write_temporary_file("mesh_item_with_bvh.glb", with_bvh);
    const std::filesystem::path without_path = write_temporary_file("mesh_item_without_bvh.glb", without_bvh);

    erhe::scene::Scene          without_scene{"without", nullptr};
    const erhe::gltf::Gltf_data without_data = parse(without_path, without_scene, fixture.store.get_resolver(), erhe::gltf::Mesh_payload_load::in_parse, erhe::gltf::Triangle_soup_reload::none);
    erhe::scene::Scene          ignored_scene{"ignored", nullptr};
    const erhe::gltf::Gltf_data ignored_data = parse(
        with_path, ignored_scene, fixture.store.get_resolver(), erhe::gltf::Mesh_payload_load::in_parse, erhe::gltf::Triangle_soup_reload::none,
        {}, erhe::gltf::Stored_bvh_use::ignore
    );
    erhe::scene::Scene          used_scene{"used", nullptr};
    const erhe::gltf::Gltf_data used_data = parse(with_path, used_scene, fixture.store.get_resolver(), erhe::gltf::Mesh_payload_load::in_parse, erhe::gltf::Triangle_soup_reload::none);
    ASSERT_TRUE(without_data.errors.empty());
    ASSERT_TRUE(ignored_data.errors.empty());
    ASSERT_TRUE(used_data.errors.empty());
    for (const char* const name : c_node_names) {
        for (const erhe::gltf::Gltf_data* data : {&without_data, &ignored_data}) {
            const std::shared_ptr<erhe::primitive::Primitive> primitive = find_node_primitive(*data, name);
            ASSERT_TRUE(primitive && primitive->render_shape) << name;
            const std::shared_ptr<const erhe::primitive::Prepared_primitive_data>& prepared = primitive->render_shape->get_prepared_data();
            ASSERT_TRUE(prepared) << name;
            EXPECT_FALSE(primitive->render_shape->has_stored_bvh()) << name;
            EXPECT_TRUE(prepared->colocation.has_value()) << name;
        }
        // The accepted BVH moves from the prepared data into the shape.
        const std::shared_ptr<erhe::primitive::Primitive> used = find_node_primitive(used_data, name);
        ASSERT_TRUE(used && used->render_shape) << name;
        ASSERT_TRUE(used->render_shape->get_prepared_data()) << name;
        EXPECT_EQ(used->render_shape->has_stored_bvh(), !erhe::raytrace::get_prebuilt_bvh_backend().empty()) << name;
    }
}

// The item form parsed through the resolver - from the file and from memory
// - loads what the file form loads: the same soups, LOD levels and prepared
// mesh data. Without the resolver the item buffers cannot be read.
TEST(Gltf_mesh_item, load_through_resolver_equals_file_form)
{
    Mesh_item_fixture fixture;
    const std::string file_glb = fixture.export_file_form();
    const std::string item_glb = fixture.export_item_form(nullptr);
    ASSERT_FALSE(file_glb.empty());
    ASSERT_FALSE(item_glb.empty());
    const std::filesystem::path file_path = write_temporary_file("mesh_item_file_form.glb", file_glb);
    const std::filesystem::path item_path = write_temporary_file("mesh_item_item_form.glb", item_glb);

    erhe::scene::Scene          file_scene{"file", nullptr};
    const erhe::gltf::Gltf_data file_data = parse(file_path, file_scene, {}, erhe::gltf::Mesh_payload_load::in_parse, erhe::gltf::Triangle_soup_reload::none);
    erhe::scene::Scene          item_scene{"item", nullptr};
    const erhe::gltf::Gltf_data item_data = parse(item_path, item_scene, fixture.store.get_resolver(), erhe::gltf::Mesh_payload_load::in_parse, erhe::gltf::Triangle_soup_reload::none);
    erhe::scene::Scene          memory_scene{"memory", nullptr};
    const std::span<const std::byte> item_bytes{reinterpret_cast<const std::byte*>(item_glb.data()), item_glb.size()};
    const erhe::gltf::Gltf_data memory_data = parse(item_path, memory_scene, fixture.store.get_resolver(), erhe::gltf::Mesh_payload_load::in_parse, erhe::gltf::Triangle_soup_reload::none, item_bytes);
    ASSERT_TRUE(file_data.errors.empty());
    ASSERT_TRUE(item_data.errors.empty());
    ASSERT_TRUE(memory_data.errors.empty());

    for (const erhe::gltf::Gltf_data* data : {&item_data, &memory_data}) {
        EXPECT_EQ(data->prepared_mesh_statistics.used_count,     file_data.prepared_mesh_statistics.used_count);
        EXPECT_EQ(data->prepared_mesh_statistics.rejected_count, 0u);
        EXPECT_EQ(data->prepared_mesh_statistics.lod_used_count, file_data.prepared_mesh_statistics.lod_used_count);
        for (const char* const name : c_node_names) {
            const std::shared_ptr<erhe::primitive::Primitive> expected = find_node_primitive(file_data, name);
            const std::shared_ptr<erhe::primitive::Primitive> actual   = find_node_primitive(*data, name);
            ASSERT_TRUE(expected && actual) << name;
            const std::shared_ptr<erhe::primitive::Triangle_soup> expected_soup = expected->render_shape->get_triangle_soup();
            const std::shared_ptr<erhe::primitive::Triangle_soup> actual_soup   = actual->render_shape->get_triangle_soup();
            ASSERT_TRUE(expected_soup && actual_soup);
            EXPECT_EQ(actual_soup->vertex_data,    expected_soup->vertex_data)    << name;
            EXPECT_EQ(actual_soup->index_data,     expected_soup->index_data)     << name;
            EXPECT_EQ(actual_soup->lod_index_data, expected_soup->lod_index_data) << name;
            EXPECT_EQ(erhe::dataformat::get_vertex_format_signature(actual_soup->vertex_format), erhe::dataformat::get_vertex_format_signature(expected_soup->vertex_format));
            const std::shared_ptr<const erhe::primitive::Prepared_primitive_data>& expected_prepared = expected->render_shape->get_prepared_data();
            const std::shared_ptr<const erhe::primitive::Prepared_primitive_data>& actual_prepared   = actual->render_shape->get_prepared_data();
            ASSERT_TRUE(expected_prepared && actual_prepared);
            EXPECT_EQ(actual_prepared->content_hash, expected_prepared->content_hash);
            EXPECT_EQ(std::memcmp(&actual_prepared->bounding_box,    &expected_prepared->bounding_box,    sizeof(expected_prepared->bounding_box)),    0);
            EXPECT_EQ(std::memcmp(&actual_prepared->bounding_sphere, &expected_prepared->bounding_sphere, sizeof(expected_prepared->bounding_sphere)), 0);
            ASSERT_TRUE(actual_prepared->colocation.has_value() && expected_prepared->colocation.has_value());
            EXPECT_EQ(actual_prepared->colocation->vertex_points, expected_prepared->colocation->vertex_points);
            EXPECT_EQ(actual_prepared->bvh.has_value(), expected_prepared->bvh.has_value());
            if (actual_prepared->bvh.has_value() && expected_prepared->bvh.has_value()) {
                expect_same_bvh(actual_prepared->bvh.value(), expected_prepared->bvh.value());
            }
            EXPECT_EQ(actual_prepared->lod.has_value(), expected_prepared->lod.has_value());
        }
    }

    erhe::scene::Scene          unresolved_scene{"unresolved", nullptr};
    const erhe::gltf::Gltf_data unresolved = parse(item_path, unresolved_scene, {}, erhe::gltf::Mesh_payload_load::in_parse, erhe::gltf::Triangle_soup_reload::none);
    EXPECT_FALSE(unresolved.errors.empty());
}

// A soup released after upload reads back from the item, bit-identical.
TEST(Gltf_mesh_item, released_soup_reads_back_from_the_item)
{
    Mesh_item_fixture fixture;
    const std::string glb = fixture.export_item_form(nullptr);
    ASSERT_FALSE(glb.empty());
    const std::filesystem::path path = write_temporary_file("mesh_item_release.glb", glb);

    erhe::scene::Scene          scene{"release", nullptr};
    const erhe::gltf::Gltf_data data = parse(path, scene, fixture.store.get_resolver(), erhe::gltf::Mesh_payload_load::in_parse, erhe::gltf::Triangle_soup_reload::record);
    ASSERT_TRUE(data.errors.empty());
    for (const char* const name : c_node_names) {
        const std::shared_ptr<erhe::primitive::Primitive> primitive = find_node_primitive(data, name);
        ASSERT_TRUE(primitive && primitive->render_shape) << name;
        const erhe::primitive::Triangle_soup original = *primitive->render_shape->get_triangle_soup();
        const std::size_t reads_before = fixture.store.get_read_count();
        ASSERT_TRUE(primitive->release_triangle_soup()) << name;
        EXPECT_TRUE(primitive->render_shape->is_triangle_soup_released());
        const std::shared_ptr<erhe::primitive::Triangle_soup> reloaded = primitive->render_shape->get_triangle_soup();
        ASSERT_TRUE(reloaded) << name;
        EXPECT_GT(fixture.store.get_read_count(), reads_before);
        EXPECT_EQ(reloaded->vertex_data,    original.vertex_data)    << name;
        EXPECT_EQ(reloaded->index_data,     original.index_data)     << name;
        EXPECT_EQ(reloaded->lod_index_data, original.lod_index_data) << name;
    }
}

// A geometry-normative primitive (a box of quads, no soup) written against the
// mesh item made from its derived triangles: the document carries no
// ERHE_geometry, and the loaded soup is the item's core.
TEST(Gltf_mesh_item, geometry_normative_primitive_round_trips_through_its_item)
{
    const std::pair<erhe::dataformat::Vertex_format, erhe::dataformat::Vertex_format> formats_pair = make_test_gpu_formats();
    const erhe::gltf::Gltf_gpu_vertex_formats formats{
        .base              = &formats_pair.first,
        .base_skinned      = &formats_pair.first,
        .optimized         = &formats_pair.second,
        .optimized_skinned = &formats_pair.second,
        .index_type        = erhe::dataformat::Format::format_32_scalar_uint,
        .encoding_version  = erhe::primitive::vertex_encoding_version
    };

    // The sinks outlive the primitive: its buffer allocations return to them.
    erhe::buffer::Cpu_buffer                vertex_buffer{"test_vertex", 64 * 1024};
    erhe::buffer::Cpu_buffer                index_buffer {"test_index",  64 * 1024};
    erhe::primitive::Cpu_vertex_buffer_sink vertex_sink{{&vertex_buffer}};
    erhe::primitive::Cpu_index_buffer_sink  index_sink {index_buffer};
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("box");
    erhe::geometry::shapes::make_box(geometry->get_mesh(), 1.0f, 2.0f, 3.0f);
    geometry->process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges |
                erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates
        }
    );
    std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(geometry);
    const erhe::primitive::Build_info build_info{
        .primitive_types = {.fill_triangles = true},
        .buffer_info = {
            .normal_style       = erhe::primitive::Normal_style::polygon_normals,
            .index_type         = erhe::dataformat::Format::format_32_scalar_uint,
            .vertex_format      = erhe::dataformat::Vertex_format{
                erhe::dataformat::Vertex_stream{
                    0,
                    {
                        {erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::position, 0},
                        {erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::normal,   0}
                    }
                }
            },
            .vertex_buffer_sink = vertex_sink,
            .index_buffer_sink  = index_sink
        },
        .normal_style = erhe::primitive::Normal_style::polygon_normals
    };
    ASSERT_TRUE(primitive->make_renderable_mesh(build_info, erhe::primitive::Normal_style::polygon_normals));
    ASSERT_FALSE(primitive->render_shape->has_triangle_soup());

    erhe::scene::Scene source_scene{"source", nullptr};
    std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>("Box");
    mesh->add_primitive(primitive, {});
    mesh->enable_flag_bits(erhe::Item_flags::content);
    mesh->set_parent(source_scene.get_root_node());

    erhe::primitive::Prepare_primitive_result result = erhe::primitive::prepare_derived_primitive_for_export(*primitive->render_shape);
    ASSERT_TRUE(result.prepared.has_value()) << result.skip_reason;
    const erhe::primitive::Prepared_primitive_export& prepared = result.prepared.value();
    const erhe::gltf::Gltf_mesh_item item = erhe::gltf::write_mesh_item(prepared, &formats);
    Memory_item_store store;
    const std::string uri = fmt::format("erhe-item:{:032x}", 7);
    store.readers[uri] = std::make_shared<Memory_item_reader>(item.payload);
    store.metas  [uri] = item.meta;

    enum class Form : unsigned int { file = 0, items };
    const auto export_document = [&](const Form form, erhe::gltf::Gltf_export_prepared_statistics* statistics) -> std::string {
        erhe::gltf::Gltf_export_arguments arguments{
            .root_node          = *source_scene.get_root_node(),
            .binary             = true,
            .prepared_mesh_data = erhe::gltf::Prepared_mesh_data::write
        };
        arguments.gpu_vertex_formats  = &formats;
        arguments.prepared_statistics = statistics;
        if (form == Form::items) {
            arguments.mesh_item_provider = [&](const erhe::primitive::Primitive_shape& shape) -> std::optional<erhe::gltf::Gltf_mesh_item_reference> {
                if (&shape != primitive->render_shape.get()) {
                    return std::nullopt;
                }
                return erhe::gltf::Gltf_mesh_item_reference{.uri = uri, .meta = store.metas.at(uri)};
            };
        }
        return erhe::gltf::export_gltf(arguments);
    };
    const std::string file_glb = export_document(Form::file, nullptr);
    erhe::gltf::Gltf_export_prepared_statistics statistics;
    const std::string item_glb = export_document(Form::items, &statistics);
    ASSERT_FALSE(file_glb.empty());
    ASSERT_FALSE(item_glb.empty());
    EXPECT_NE(glb_json_chunk(file_glb).find("ERHE_geometry"), std::string::npos);
    EXPECT_EQ(glb_json_chunk(item_glb).find("ERHE_geometry"), std::string::npos);
    EXPECT_NE(glb_json_chunk(item_glb).find(uri), std::string::npos);
    EXPECT_EQ(statistics.mesh_item_count, 1u);
    EXPECT_FALSE(glb_has_bin_chunk(item_glb));

    const std::filesystem::path path = write_temporary_file("mesh_item_geometry.glb", item_glb);
    erhe::scene::Scene    scene{"items", nullptr};
    erhe::gltf::Gltf_data data = parse(path, scene, store.get_resolver(), erhe::gltf::Mesh_payload_load::in_parse, erhe::gltf::Triangle_soup_reload::none);
    ASSERT_TRUE(data.errors.empty());
    const std::shared_ptr<erhe::primitive::Primitive> loaded = find_node_primitive(data, "Box");
    ASSERT_TRUE(loaded && loaded->render_shape);
    const std::shared_ptr<erhe::primitive::Triangle_soup> soup = loaded->render_shape->get_triangle_soup();
    ASSERT_TRUE(soup);
    EXPECT_EQ(soup->index_data,         prepared.core.index_data);
    EXPECT_EQ(soup->get_vertex_count(), prepared.core.get_vertex_count());
    // The anisotropy control stream travels as _ANISO_CONTROL.
    EXPECT_NE(glb_json_chunk(item_glb).find("_ANISO_CONTROL"), std::string::npos);
    EXPECT_NE(soup->vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_aniso_control).attribute, nullptr);
    const std::shared_ptr<const erhe::primitive::Prepared_primitive_data>& prepared_data = loaded->render_shape->get_prepared_data();
    ASSERT_TRUE(prepared_data);
    EXPECT_EQ(prepared_data->content_hash, prepared.content_hash);
    ASSERT_TRUE(prepared_data->colocation.has_value());
    EXPECT_EQ(prepared_data->colocation->vertex_points, prepared.vertex_points);
    erhe::gltf::close_source_files(data);
}

} // anonymous namespace
