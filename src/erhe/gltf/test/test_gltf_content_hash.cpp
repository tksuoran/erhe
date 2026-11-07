// R3.5 content hashes as loaded state (doc/erhe/gltf.md "Content hashes"):
// the parse computes the hash of each soup it reads.

#include "gltf_test_util.hpp"

#include "erhe_hash/content_hash.hpp"
#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <gtest/gtest.h>

#include <taskflow/taskflow.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using erhe_gltf_test::data_path;
using erhe_gltf_test::write_temporary_file;

[[nodiscard]] auto parse(
    const std::filesystem::path&           path,
    erhe::scene::Scene&                    scene,
    const erhe::gltf::Gltf_content_hash_load content_hashes,
    const erhe::gltf::Mesh_payload_load    mesh_payload = erhe::gltf::Mesh_payload_load::in_parse
) -> erhe::gltf::Gltf_data
{
    tf::Executor executor{1};
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    return erhe::gltf::parse_gltf(
        erhe::gltf::Gltf_parse_arguments{
            .executor       = executor,
            .root_node      = root_node,
            .path           = path,
            .parallel       = false,
            .content_hashes = content_hashes,
            .mesh_payload   = mesh_payload
        }
    );
}

[[nodiscard]] auto get_shapes(const erhe::gltf::Gltf_data& data) -> std::vector<std::shared_ptr<erhe::primitive::Primitive_render_shape>>
{
    std::vector<std::shared_ptr<erhe::primitive::Primitive_render_shape>> shapes;
    for (const std::shared_ptr<erhe::scene::Node>& node : data.nodes) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
        if (!mesh) {
            continue;
        }
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (mesh_primitive.primitive && mesh_primitive.primitive->render_shape) {
                shapes.push_back(mesh_primitive.primitive->render_shape);
            }
        }
    }
    return shapes;
}

[[nodiscard]] auto export_prepared(const erhe::scene::Scene& scene) -> std::string
{
    const erhe::gltf::Gltf_export_arguments arguments{
        .root_node          = *scene.get_root_node(),
        .binary             = true,
        .prepared_mesh_data = erhe::gltf::Prepared_mesh_data::write
    };
    return erhe::gltf::export_gltf(arguments);
}

} // anonymous namespace

TEST(Gltf_content_hash, the_parse_hashes_the_soups_it_reads)
{
    erhe::scene::Scene scene{"source", nullptr};
    const erhe::gltf::Gltf_data data = parse(data_path("variants.gltf"), scene, erhe::gltf::Gltf_content_hash_load::compute);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive_render_shape>> shapes = get_shapes(data);
    ASSERT_FALSE(shapes.empty());
    for (const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape : shapes) {
        const std::optional<erhe::hash::Content_hash> hash = shape->get_content_hash();
        ASSERT_TRUE(hash.has_value());
        EXPECT_EQ(hash.value(), erhe::primitive::compute_triangle_soup_content_hash(*shape->get_triangle_soup()));
    }

    erhe::scene::Scene none_scene{"none", nullptr};
    const erhe::gltf::Gltf_data none_data = parse(data_path("variants.gltf"), none_scene, erhe::gltf::Gltf_content_hash_load::none);
    for (const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape : get_shapes(none_data)) {
        EXPECT_FALSE(shape->get_content_hash().has_value());
    }
}

// A derived document (a Scene item, a mesh item) stores no hash; a parse of it hashes the core it holds.
TEST(Gltf_content_hash, a_derived_export_stores_no_hash)
{
    erhe::scene::Scene source_scene{"source", nullptr};
    const erhe::gltf::Gltf_data source = parse(data_path("variants.gltf"), source_scene, erhe::gltf::Gltf_content_hash_load::compute);
    const std::string saved = export_prepared(source_scene);
    ASSERT_FALSE(saved.empty());
    EXPECT_EQ(saved.find("source_content_hash"), std::string::npos);

    erhe::scene::Scene reopened_scene{"reopened", nullptr};
    const erhe::gltf::Gltf_data reopened = parse(write_temporary_file("content_hash_derived.glb", saved), reopened_scene, erhe::gltf::Gltf_content_hash_load::compute);
    for (const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape : get_shapes(reopened)) {
        ASSERT_TRUE(shape->get_content_hash().has_value());
        EXPECT_EQ(shape->get_content_hash().value(), erhe::primitive::compute_triangle_soup_content_hash(*shape->get_triangle_soup()));
    }

    for (const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape : get_shapes(source)) {
        ASSERT_TRUE(shape->make_geometry());
        erhe::primitive::Prepare_primitive_result result = erhe::primitive::prepare_primitive_for_export(*shape);
        ASSERT_TRUE(result.prepared.has_value());
        const erhe::gltf::Gltf_mesh_item item = erhe::gltf::write_mesh_item(result.prepared.value(), nullptr);
        EXPECT_EQ(item.meta.find("source_content_hash"), std::string::npos);
    }
}

// The content hash is the shape's own: a Geometry-derived one goes with an in-place edit, a soup's stays.
TEST(Gltf_content_hash, an_in_place_edit_drops_a_derived_hash_only)
{
    erhe::scene::Scene source_scene{"source", nullptr};
    const erhe::gltf::Gltf_data source = parse(data_path("variants.gltf"), source_scene, erhe::gltf::Gltf_content_hash_load::compute);
    const std::vector<std::shared_ptr<erhe::primitive::Primitive_render_shape>> shapes = get_shapes(source);
    ASSERT_FALSE(shapes.empty());
    const erhe::hash::Content_hash before = shapes.front()->get_content_hash().value();
    shapes.front()->mark_prepared_data_stale();
    ASSERT_TRUE(shapes.front()->get_content_hash().has_value());
    EXPECT_EQ(shapes.front()->get_content_hash().value(), before);
}
