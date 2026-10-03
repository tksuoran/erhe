// parse_gltf against checked-in fixtures (src/erhe/gltf/test/data).

#include "gltf_test_util.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <gtest/gtest.h>

namespace {

using erhe_gltf_test::data_path;
using erhe_gltf_test::parse_file;

// variants.gltf: one node "Panel" with mesh "Panel" of two primitives (four
// vertices, six indices each), three materials Plain / Red / Green, and the
// KHR_materials_variants red (both primitives -> Red) and green (primitive
// 0 -> Green).
TEST(Gltf_parse, variants_fixture_structure)
{
    erhe::scene::Scene          scene{"test", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(data_path("variants.gltf"), scene);

    ASSERT_EQ(data.nodes.size(), 1u);
    ASSERT_TRUE(data.nodes[0]);
    EXPECT_EQ(data.nodes[0]->get_name(), "Panel");
    EXPECT_EQ(data.nodes[0]->get_parent_node(), scene.get_root_node());

    ASSERT_EQ(data.materials.size(), 3u);
    EXPECT_EQ(data.materials[0]->get_name(), "Plain");
    EXPECT_EQ(data.materials[1]->get_name(), "Red");
    EXPECT_EQ(data.materials[2]->get_name(), "Green");

    ASSERT_EQ(data.meshes.size(), 1u);
    const std::shared_ptr<erhe::scene::Mesh>& mesh = data.meshes[0];
    ASSERT_TRUE(mesh);
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    ASSERT_EQ(primitives.size(), 2u);
    for (const erhe::scene::Mesh_primitive& mesh_primitive : primitives) {
        EXPECT_EQ(mesh_primitive.material, data.materials[0]);
        ASSERT_TRUE(mesh_primitive.primitive);
        const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape =
            mesh_primitive.primitive->get_render_shape(erhe::primitive::Mesh_variant::original);
        ASSERT_TRUE(shape);
        const std::shared_ptr<erhe::primitive::Triangle_soup>& soup = shape->get_triangle_soup();
        ASSERT_TRUE(soup);
        EXPECT_EQ(soup->get_vertex_count(), 4u);
        EXPECT_EQ(soup->get_index_count(), 6u);
    }
    // Gltf_data::meshes holds one template per glTF mesh; the node
    // instantiates its own Mesh prim from it, sharing the primitives.
    const std::shared_ptr<erhe::scene::Mesh> node_mesh = erhe::scene::get_mesh(data.nodes[0].get());
    ASSERT_TRUE(node_mesh);
    EXPECT_NE(node_mesh, mesh);
    ASSERT_EQ(node_mesh->get_primitives().size(), primitives.size());
    for (std::size_t i = 0; i < primitives.size(); ++i) {
        EXPECT_EQ(node_mesh->get_primitives()[i].primitive, primitives[i].primitive);
    }
}

TEST(Gltf_parse, variants_fixture_material_variants)
{
    erhe::scene::Scene          scene{"test", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(data_path("variants.gltf"), scene);

    ASSERT_EQ(data.nodes.size(), 1u);
    ASSERT_EQ(data.materials.size(), 3u);
    ASSERT_EQ(data.material_variants.size(), 2u);
    // Bindings name the Mesh prim the node instantiated, not the template.
    const std::shared_ptr<erhe::scene::Mesh> node_mesh = erhe::scene::get_mesh(data.nodes[0].get());
    ASSERT_TRUE(node_mesh);

    const erhe::gltf::Gltf_material_variant& red = data.material_variants[0];
    EXPECT_EQ(red.name, "red");
    ASSERT_EQ(red.bindings.size(), 2u);
    for (std::size_t i = 0; i < red.bindings.size(); ++i) {
        EXPECT_EQ(red.bindings[i].mesh, node_mesh);
        EXPECT_EQ(red.bindings[i].material, data.materials[1]);
    }
    EXPECT_NE(red.bindings[0].primitive_index, red.bindings[1].primitive_index);

    const erhe::gltf::Gltf_material_variant& green = data.material_variants[1];
    EXPECT_EQ(green.name, "green");
    ASSERT_EQ(green.bindings.size(), 1u);
    EXPECT_EQ(green.bindings[0].mesh, node_mesh);
    EXPECT_EQ(green.bindings[0].primitive_index, 0u);
    EXPECT_EQ(green.bindings[0].material, data.materials[2]);
}

// Each primitive builds a Geometry from its triangle soup: the editor's mesh
// operations work on that Geometry, and the async mesh operation that
// failed on this file (memory-bank topic scenes_and_assets) starts here.
TEST(Gltf_parse, variants_fixture_primitives_build_geometry)
{
    erhe::scene::Scene          scene{"test", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(data_path("variants.gltf"), scene);

    ASSERT_EQ(data.meshes.size(), 1u);
    for (const erhe::scene::Mesh_primitive& mesh_primitive : data.meshes[0]->get_primitives()) {
        ASSERT_TRUE(mesh_primitive.primitive);
        const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape =
            mesh_primitive.primitive->get_render_shape(erhe::primitive::Mesh_variant::original);
        ASSERT_TRUE(shape);
        const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape->get_geometry();
        ASSERT_TRUE(geometry);
        // Four vertices, two triangles.
        EXPECT_EQ(geometry->get_mesh().vertices.nb(), 4u);
        EXPECT_EQ(geometry->get_mesh().facets.nb(),   2u);
    }
}

// out_of_range_indices.gltf is variants.gltf with the second primitive's
// indices 4..7 addressing its 4-element POSITION accessor (invalid glTF).
// The primitive is skipped instead of reading past the vertex data.
TEST(Gltf_parse, out_of_range_indices_skip_primitive)
{
    erhe::scene::Scene          scene{"test", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(data_path("out_of_range_indices.gltf"), scene);

    ASSERT_EQ(data.nodes.size(), 1u);
    const std::shared_ptr<erhe::scene::Mesh> node_mesh = erhe::scene::get_mesh(data.nodes[0].get());
    ASSERT_TRUE(node_mesh);
    ASSERT_EQ(node_mesh->get_primitives().size(), 1u);
    const std::shared_ptr<erhe::primitive::Primitive_render_shape>& shape =
        node_mesh->get_primitives()[0].primitive->get_render_shape(erhe::primitive::Mesh_variant::original);
    ASSERT_TRUE(shape);
    const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape->get_geometry();
    ASSERT_TRUE(geometry);
    EXPECT_EQ(geometry->get_mesh().vertices.nb(), 4u);
    EXPECT_EQ(geometry->get_mesh().facets.nb(),   2u);
}

} // anonymous namespace
