// The editor's own loading (doc/editor/prepared_assets.md "The editor's own
// loading"): the first open of a source derives each soup's items and stores
// them, the second loads them, and both give the primitive the item's core
// keyed as the source with the source soup as its authoring soup. The GPU
// builds between prepare_job() and install() are the load task's and are not
// run here.

#include "assets/asset_paths.hpp"
#include "assets/editor_asset.hpp"
#include "assets/editor_asset_load.hpp"
#include "assets/item_keys.hpp"
#include "assets/local_mesh_items.hpp"

#include "erhe_asset_store/asset_store.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_hash/content_hash.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_raytrace/prebuilt_bvh.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <gtest/gtest.h>

#include <taskflow/taskflow.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

using editor::Editor_asset_job;
using editor::Editor_asset_job_kind;
using editor::Editor_asset_load;
using editor::Editor_asset_load_counts;
using editor::Editor_asset_load_create_info;
using editor::Editor_asset_registry;

// A grid of quads with positions, normals and texcoords, its corners duplicated per triangle.
[[nodiscard]] auto make_soup(const int size) -> std::shared_ptr<erhe::primitive::Triangle_soup>
{
    using erhe::dataformat::Format;
    using erhe::dataformat::Vertex_attribute_usage;
    std::shared_ptr<erhe::primitive::Triangle_soup> soup = std::make_shared<erhe::primitive::Triangle_soup>();
    erhe::dataformat::Vertex_stream stream{0};
    stream.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::position,  0);
    stream.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::normal,    0);
    stream.emplace_back(Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0);
    stream.finalize_stride();
    soup->vertex_format.streams.push_back(std::move(stream));
    const std::size_t stride = soup->vertex_format.streams.front().stride;
    const auto add_vertex = [&soup, stride](const float x, const float y) -> uint32_t {
        const std::size_t vertex = soup->vertex_data.size() / stride;
        soup->vertex_data.resize((vertex + 1) * stride, uint8_t{0});
        const float values[8] = {x, y, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f * x, 0.5f * y};
        std::memcpy(soup->vertex_data.data() + (vertex * stride), values, sizeof(values));
        return static_cast<uint32_t>(vertex);
    };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float fx = static_cast<float>(x);
            const float fy = static_cast<float>(y);
            const uint32_t a = add_vertex(fx, fy);
            const uint32_t b = add_vertex(fx + 1.0f, fy);
            const uint32_t c = add_vertex(fx + 1.0f, fy + 1.0f);
            const uint32_t d = add_vertex(fx, fy + 1.0f);
            soup->index_data.insert(soup->index_data.end(), {a, b, c, a, c, d});
        }
    }
    return soup;
}

[[nodiscard]] auto get_primitives(const erhe::gltf::Gltf_data& data) -> std::vector<std::shared_ptr<erhe::primitive::Primitive>>
{
    std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives;
    for (const std::shared_ptr<erhe::scene::Node>& node : data.nodes) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = node ? erhe::scene::get_mesh(node.get()) : std::shared_ptr<erhe::scene::Mesh>{};
        if (!mesh) {
            continue;
        }
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            primitives.push_back(mesh_primitive.primitive);
        }
    }
    return primitives;
}

class Editor_asset_load_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = editor::normalize_asset_path(std::filesystem::temp_directory_path() / ("editor_asset_load_test_" + std::to_string(tick)));
        std::filesystem::create_directories(m_root);
        m_source = m_root / "grids.glb";
        // Two soup meshes; written raw, as a source save writes them.
        erhe::scene::Scene scene{"grids", nullptr};
        for (const int size : {2, 3}) {
            std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>("grid " + std::to_string(size));
            mesh->add_primitive(std::make_shared<erhe::primitive::Primitive>(make_soup(size)));
            mesh->enable_flag_bits(erhe::Item_flags::content);
            mesh->set_parent(scene.get_root_node());
        }
        const std::string glb = erhe::gltf::export_gltf(*scene.get_root_node(), true);
        ASSERT_FALSE(glb.empty());
        std::ofstream{m_source, std::ios::binary}.write(glb.data(), static_cast<std::streamsize>(glb.size()));
    }
    void TearDown() override
    {
        std::error_code error_code;
        std::filesystem::remove_all(m_root, error_code);
    }

    // A deferred parse that computes the content hashes, as a source open's.
    [[nodiscard]] auto parse(erhe::scene::Scene& scene) -> erhe::gltf::Gltf_data
    {
        const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
        return erhe::gltf::parse_gltf(
            erhe::gltf::Gltf_parse_arguments{
                .executor             = m_executor,
                .root_node            = root_node,
                .path                 = m_source,
                .parallel             = false,
                .content_hashes       = erhe::gltf::Gltf_content_hash_load::compute,
                .triangle_soup_reload = erhe::gltf::Triangle_soup_reload::record,
                .mesh_payload         = erhe::gltf::Mesh_payload_load::deferred
            }
        );
    }

    [[nodiscard]] auto make_load(const std::shared_ptr<editor::Editor_asset>& asset, const std::size_t job_count) -> std::unique_ptr<Editor_asset_load>
    {
        return std::make_unique<Editor_asset_load>(Editor_asset_load_create_info{
            .editor_asset       = asset,
            .catalog            = asset->get_item_catalog(),
            .gpu_vertex_formats = {},
            .mesh_stamp         = editor::make_local_mesh_stamp(editor::make_vertex_layout_signature(nullptr)),
            .bvh_stamp          = editor::make_raytrace_bvh_stamp(erhe::raytrace::get_prebuilt_bvh_backend(), erhe::raytrace::get_prebuilt_bvh_library()),
            .bvh_items          = !erhe::raytrace::get_prebuilt_bvh_backend().empty(),
            .executor           = &m_executor,
            .job_count          = job_count
        });
    }

    // Every job of one open: prepare, then install as the load task does after its build.
    [[nodiscard]] auto open(Editor_asset_registry& registry, erhe::scene::Scene& scene, erhe::gltf::Gltf_data& out_data, std::vector<Editor_asset_job>& out_jobs) -> Editor_asset_load_counts
    {
        out_data = parse(scene);
        EXPECT_TRUE(out_data.errors.empty());
        const std::shared_ptr<editor::Editor_asset> asset = registry.get(m_source, {});
        EXPECT_TRUE(asset);
        const std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives = get_primitives(out_data);
        const std::unique_ptr<Editor_asset_load> load = make_load(asset, primitives.size());
        for (const std::shared_ptr<erhe::primitive::Primitive>& primitive : primitives) {
            Editor_asset_job job = load->prepare_job(*primitive);
            if (job.kind == Editor_asset_job_kind::item) {
                // The build reads the item's payload first.
                EXPECT_TRUE(job.item_primitive->load_payload());
                load->install(*primitive, job);
            } else {
                load->count_as_before(job.reason);
            }
            out_jobs.push_back(std::move(job));
            load->finish_job();
        }
        load->share_replaced_shapes(out_data);
        return load->get_counts();
    }

    tf::Executor          m_executor{1};
    std::filesystem::path m_root;
    std::filesystem::path m_source;
};

} // anonymous namespace

TEST_F(Editor_asset_load_test, first_open_derives_and_stores_the_second_loads)
{
    const bool bvh_items = !erhe::raytrace::get_prebuilt_bvh_backend().empty();
    std::vector<erhe::hash::Content_hash> source_hashes;
    {
        erhe::scene::Scene scene{"first", nullptr};
        const erhe::gltf::Gltf_data data = parse(scene);
        for (const std::shared_ptr<erhe::primitive::Primitive>& primitive : get_primitives(data)) {
            ASSERT_TRUE(primitive->load_payload());
            source_hashes.push_back(primitive->render_shape->get_content_hash().value());
        }
    }
    ASSERT_EQ(source_hashes.size(), 2u);

    Editor_asset_registry registry{m_root / "cache"};
    {
        erhe::scene::Scene            scene{"first open", nullptr};
        erhe::gltf::Gltf_data         data;
        std::vector<Editor_asset_job> jobs;
        const Editor_asset_load_counts counts = open(registry, scene, data, jobs);
        EXPECT_EQ(counts.derived_stored, 2u);
        EXPECT_EQ(counts.from_items, 0u);
        EXPECT_EQ(counts.bvh_built_stored, bvh_items ? 2u : 0u);
        EXPECT_EQ(counts.items_written, bvh_items ? 4u : 2u);
        EXPECT_EQ(counts.transactions, 1u); // one batch for the whole load
        const std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives = get_primitives(data);
        ASSERT_EQ(primitives.size(), 2u);
        for (std::size_t i = 0; i < 2; ++i) {
            const Editor_asset_job& job = jobs[i];
            ASSERT_EQ(job.kind, Editor_asset_job_kind::item);
            EXPECT_TRUE(job.derived_mesh);
            // The one key computation of Export and the load.
            EXPECT_EQ(job.mesh_key, editor::make_shape_local_mesh_key(editor::Local_mesh_source::soup, source_hashes[i], {}, editor::make_local_mesh_stamp(editor::make_vertex_layout_signature(nullptr))));
            const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape = primitives[i]->render_shape;
            EXPECT_TRUE(primitives[i]->is_prepared());
            EXPECT_EQ(shape->get_content_hash().value(), source_hashes[i]);
            ASSERT_TRUE(shape->has_authoring_soup());
            const std::shared_ptr<erhe::primitive::Triangle_soup> authoring = shape->get_authoring_triangle_soup();
            ASSERT_TRUE(authoring);
            EXPECT_EQ(erhe::primitive::compute_triangle_soup_content_hash(*authoring), source_hashes[i]);
            // The core is welded: fewer vertices than the soup's per-triangle corners.
            EXPECT_LT(shape->get_triangle_soup()->get_vertex_count(), authoring->get_vertex_count());
        }
        const std::shared_ptr<editor::Editor_asset> asset = registry.get(m_source, {});
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            const Editor_asset_job& job = jobs[i];
            EXPECT_TRUE(asset->get_store().contains(job.mesh_key));
            EXPECT_EQ(asset->get_store().contains(job.bvh_key), bvh_items);
            // Export of the loaded core makes the stored item again from its authoring soup: the same bytes.
            const editor::Local_mesh_items items = editor::make_local_mesh_items(
                *primitives[i]->render_shape, editor::Local_mesh_source::soup, {}, nullptr, editor::Local_mesh_items_wanted::mesh
            );
            ASSERT_TRUE(items.mesh.has_value()) << items.skip_reason;
            EXPECT_EQ(asset->get_store().read_meta(job.mesh_key).value(), items.mesh->meta);
            std::vector<std::byte> stored(items.mesh->payload.size());
            ASSERT_TRUE(asset->get_store().read_payload(job.mesh_key, 0, stored));
            EXPECT_EQ(stored, items.mesh->payload);
        }
    }
    {
        erhe::scene::Scene            scene{"second open", nullptr};
        erhe::gltf::Gltf_data         data;
        std::vector<Editor_asset_job> jobs;
        const Editor_asset_load_counts counts = open(registry, scene, data, jobs);
        EXPECT_EQ(counts.from_items, 2u);
        EXPECT_EQ(counts.derived_stored, 0u);
        EXPECT_EQ(counts.bvh_from_items, bvh_items ? 2u : 0u);
        EXPECT_EQ(counts.bvh_built_stored, 0u);
        EXPECT_EQ(counts.items_written, 0u);
        EXPECT_EQ(counts.transactions, 0u);
        const std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives = get_primitives(data);
        for (std::size_t i = 0; i < primitives.size(); ++i) {
            EXPECT_FALSE(jobs[i].derived_mesh);
            EXPECT_TRUE(jobs[i].mesh_present);
            EXPECT_TRUE(primitives[i]->is_prepared());
            EXPECT_EQ(primitives[i]->render_shape->get_content_hash().value(), source_hashes[i]);
            EXPECT_TRUE(primitives[i]->render_shape->has_authoring_soup());
        }
    }
}

// A LOD request is a key input: the item of a request is another item.
TEST_F(Editor_asset_load_test, a_lod_request_asks_for_its_own_item)
{
    Editor_asset_registry registry{m_root / "cache"};
    erhe::scene::Scene            scene{"plain", nullptr};
    erhe::gltf::Gltf_data         data;
    std::vector<Editor_asset_job> jobs;
    static_cast<void>(open(registry, scene, data, jobs));

    erhe::scene::Scene lod_scene{"lod", nullptr};
    erhe::gltf::Gltf_data lod_data = parse(lod_scene);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives = get_primitives(lod_data);
    ASSERT_FALSE(primitives.empty());
    erhe::primitive::Lod_settings request{};
    request.level_count = 2;
    request.ratio       = 0.5f;
    primitives.front()->lod_settings = request;
    const std::shared_ptr<editor::Editor_asset> asset = registry.get(m_source, {});
    const std::unique_ptr<Editor_asset_load> load = make_load(asset, 1);
    const Editor_asset_job job = load->prepare_job(*primitives.front());
    load->finish_job();
    EXPECT_EQ(job.kind, Editor_asset_job_kind::item);
    EXPECT_FALSE(job.mesh_present);
    EXPECT_TRUE(job.derived_mesh);
    EXPECT_NE(job.mesh_key, jobs.front().mesh_key);
    EXPECT_TRUE(asset->get_store().contains(job.mesh_key));
}

// A geometry-normative primitive is built from its Geometry as before.
TEST_F(Editor_asset_load_test, a_primitive_without_a_soup_payload_is_built_as_before)
{
    Editor_asset_registry registry{m_root / "cache"};
    const std::shared_ptr<editor::Editor_asset> asset = registry.get(m_source, {});
    const std::unique_ptr<Editor_asset_load> load = make_load(asset, 1);
    erhe::primitive::Primitive resident{make_soup(2)}; // a soup the parse read: no payload left for the job
    const Editor_asset_job job = load->prepare_job(resident);
    load->count_as_before(job.reason);
    load->finish_job();
    EXPECT_EQ(job.kind, Editor_asset_job_kind::as_before);
    EXPECT_EQ(load->get_counts().built_as_before, 1u);
    EXPECT_EQ(load->get_counts().items_written, 0u);
}
