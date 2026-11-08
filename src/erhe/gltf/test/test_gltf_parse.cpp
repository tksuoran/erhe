// parse_gltf against checked-in fixtures (src/erhe/gltf/test/data).

#include "gltf_test_util.hpp"

#include "erhe_file/file.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_gltf/gltf_uri.hpp"
#include "erhe_graphics/image_loader.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <gtest/gtest.h>

#include <fmt/format.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

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

// A 2x2 RGBA PNG.
constexpr unsigned char c_png_2x2[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D,
    0x24, 0x00, 0x00, 0x00, 0x10, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
    0x1F, 0x0A, 0x21, 0x14, 0x00, 0x45, 0xCC, 0x07, 0xF9, 0x8E, 0xCA, 0x50, 0xB6, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};

class Named_image
{
public:
    std::string file_name;
    std::string uri;
};

// An external image whose name holds characters a URI reserves (written
// percent-encoded) is found and decodes, as a plain name does; so is a
// glTF 2.1 files entry (doc/erhe/gltf.md "File uris").
TEST(Gltf_parse, image_and_file_uris_decode_after_the_split)
{
    std::vector<Named_image> images{
        {.file_name = "plain.png",   .uri = "plain.png"},
        {.file_name = "img #1%.png", .uri = "img%20%231%25.png"},
#if !defined(_WIN32) // '?' is no file name character on Windows
        {.file_name = "img #1?.png", .uri = "img%20%231%3F.png"},
#endif
    };
    std::string images_json;
    std::string materials_json;
    std::string textures_json;
    for (std::size_t i = 0, end = images.size(); i < end; ++i) {
        std::ofstream file{erhe_gltf_test::process_temporary_directory() / erhe::file::from_string(images[i].file_name), std::ios::binary};
        file.write(reinterpret_cast<const char*>(c_png_2x2), sizeof(c_png_2x2));
        const char* const separator = (i == 0) ? "" : ",";
        images_json    += fmt::format(R"({}{{"uri":"{}"}})", separator, images[i].uri);
        textures_json  += fmt::format(R"({}{{"source":{}}})", separator, i);
        materials_json += fmt::format(R"({}{{"name":"m{}","pbrMetallicRoughness":{{"baseColorTexture":{{"index":{}}}}}}})", separator, i, i);
    }
    const std::string gltf = fmt::format(
        R"({{"asset":{{"version":"2.1"}},"images":[{}],"textures":[{}],"materials":[{}],)"
        R"("files":[{{"uri":"sub%20%232%3F.gltf","mimeType":"model/gltf+json"}},{{"uri":"plain.gltf","mimeType":"model/gltf+json"}}]}})",
        images_json, textures_json, materials_json
    );
    const std::filesystem::path path = erhe_gltf_test::write_temporary_file("image uris.gltf", gltf);

    erhe::scene::Scene          scene{"image_uris", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(path, scene);
    EXPECT_TRUE(data.errors.empty());
    ASSERT_EQ(data.image_sources.size(), images.size());
    for (std::size_t i = 0, end = images.size(); i < end; ++i) {
        const std::shared_ptr<erhe::gltf::Gltf_image_source>& source = data.image_sources[i];
        ASSERT_TRUE(source) << images[i].file_name << " is found";
        EXPECT_EQ(erhe::file::to_string(source->file_path.filename()), images[i].file_name);
        std::vector<std::byte> bytes;
        ASSERT_TRUE(source->read(bytes));
        erhe::graphics::Image_loader loader;
        erhe::graphics::Image_info   info;
        const std::span<const std::uint8_t> encoded{reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()};
        ASSERT_TRUE(loader.open(encoded, info, false)) << images[i].file_name << " decodes";
        EXPECT_EQ(info.width,  2);
        EXPECT_EQ(info.height, 2);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(info.row_stride) * static_cast<std::size_t>(info.height));
        EXPECT_TRUE(loader.load(pixels));
        loader.close();
    }

    ASSERT_EQ(data.files.size(), 2u);
    EXPECT_EQ(erhe::file::to_string(data.files[0].resolved_path.filename()), "sub #2?.gltf");
    EXPECT_EQ(erhe::file::to_string(data.files[1].resolved_path.filename()), "plain.gltf");
}

// The components split before the path is decoded; a non-file uri names no file.
TEST(Gltf_parse, uri_to_file_path)
{
    using erhe::gltf::gltf_uri_to_file_path;
    const auto path_of = [](const std::string_view uri) -> std::string {
        const std::optional<std::filesystem::path> path = gltf_uri_to_file_path(uri);
        return path.has_value() ? erhe::file::to_string(path.value()) : std::string{"<none>"};
    };
    EXPECT_EQ(path_of("a.png"),                     "a.png");
    EXPECT_EQ(path_of("sub/a%20%231%3F.png"),       "sub/a #1?.png");
    EXPECT_EQ(path_of("a.png?query#fragment"),      "a.png");
    EXPECT_EQ(path_of("a.png#fragment"),            "a.png");
    EXPECT_EQ(path_of("a%2.png"),                   "a%2.png");
    EXPECT_EQ(path_of("file:///models/a%20b.glb"),  "/models/a b.glb");
    EXPECT_EQ(path_of("file:///C:/models/a.glb"),   "C:/models/a.glb");
    EXPECT_EQ(path_of("C:/models/a.glb"),           "C:/models/a.glb");
    EXPECT_EQ(path_of("data:image/png;base64,AAA"), "<none>");
    EXPECT_EQ(path_of("https://example.com/a.png"), "<none>");
    EXPECT_EQ(path_of("file://localhost/x/a.png"),  "/x/a.png");
#if defined(_WIN32)
    EXPECT_EQ(path_of("file://srv/share/a%20b.png"), "//srv/share/a b.png");
#else
    EXPECT_EQ(path_of("file://srv/share/a.png"),    "<none>");
#endif
    EXPECT_EQ(path_of("erhe-item:0123abcd"),        "<none>");
    EXPECT_EQ(path_of(""),                          "<none>");

    EXPECT_EQ(erhe::gltf::make_gltf_file_uri(std::filesystem::path{"sub dir"} / "a #1%.png"), "sub%20dir/a%20%231%25.png");
    EXPECT_EQ(path_of(erhe::gltf::make_gltf_file_uri(std::filesystem::path{"sub dir"} / "a #1%.png")), "sub dir/a #1%.png");

    // Absolute paths are file: uris and read back.
    const auto round_trip = [&path_of](const std::string& text) -> std::string {
        return path_of(erhe::gltf::make_gltf_file_uri(erhe::file::from_string(text)));
    };
    EXPECT_EQ(erhe::gltf::make_gltf_file_uri(erhe::file::from_string("/x/a #1.gltf")), "file:///x/a%20%231.gltf");
    EXPECT_EQ(round_trip("/x/a #1.gltf"), "/x/a #1.gltf");
    EXPECT_EQ(erhe::gltf::make_gltf_file_uri(erhe::file::from_string("C:/x/lib #1.gltf")), "file:///C:/x/lib%20%231.gltf");
    EXPECT_EQ(round_trip("C:/x/lib #1.gltf"), "C:/x/lib #1.gltf");
#if defined(_WIN32)
    EXPECT_EQ(erhe::gltf::make_gltf_file_uri(erhe::file::from_string("//srv/share/x #1.gltf")), "file://srv/share/x%20%231.gltf");
    EXPECT_EQ(round_trip("//srv/share/x #1.gltf"), "//srv/share/x #1.gltf");
#else
    // POSIX: a leading "//" is the root directory, not a server.
    EXPECT_EQ(erhe::gltf::make_gltf_file_uri(erhe::file::from_string("//srv/share/x #1.gltf")), "file:///srv/share/x%20%231.gltf");
    EXPECT_EQ(round_trip("//srv/share/x #1.gltf"), "/srv/share/x #1.gltf");
#endif
}

} // anonymous namespace
