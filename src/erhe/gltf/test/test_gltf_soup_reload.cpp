// Triangle soup reload (doc/erhe/gltf.md "Triangle soup reload"): a parse
// with Triangle_soup_reload::record gives every soup primitive a source that
// reads its accessors' bytes back from the file, and the reloaded soup is
// bit-identical to the parsed one. A parse from a file reads the soups
// through those ranges too, without loading the buffers (doc/erhe/gltf.md
// "Reading buffers by range"); it gives the same soups as a parse of the
// same bytes from memory, which loads every buffer.

#include "gltf_test_util.hpp"

#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <gtest/gtest.h>
#include <taskflow/taskflow.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <fstream>
#include <string>
#include <vector>

namespace {

using erhe::primitive::Triangle_soup;

// One buffer, four bufferViews:
// - view 0: 8 interleaved vertices (POSITION, NORMAL, TEXCOORD_0; stride 32)
// - view 1: two uint16 index accessors (offsets 0 and 12), 6 indices each
// - view 2: COLOR_0 as normalized uint8 vec4, tightly packed
// - view 3: 3 uint8 indices
// Primitives 0 and 1 share views 0 and 1; primitive 2 reads POSITION from
// view 0 too.
constexpr std::size_t c_buffer_size = 352; // 316 bytes of mesh data, padding, 32 bytes of animation data

[[nodiscard]] auto make_buffer() -> std::vector<std::byte>
{
    std::vector<std::byte> bytes(c_buffer_size, std::byte{0});
    for (std::size_t v = 0; v < 8; ++v) {
        const float x = static_cast<float>(v % 4) * 0.5f;
        const float y = static_cast<float>(v / 4) * 1.25f;
        const float values[8] = {x, y, 0.125f * static_cast<float>(v), 0.0f, 0.0f, 1.0f, x * 0.3f, 1.0f - (y * 0.7f)};
        std::memcpy(bytes.data() + (v * 32), &values[0], sizeof(values));
    }
    const uint16_t indices[12] = {0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5};
    std::memcpy(bytes.data() + 256, &indices[0], sizeof(indices));
    for (std::size_t v = 0; v < 8; ++v) {
        const uint8_t color[4] = {static_cast<uint8_t>(30 * v), static_cast<uint8_t>(255 - (20 * v)), 7, 255};
        std::memcpy(bytes.data() + 280 + (v * 4), &color[0], 4);
    }
    const uint8_t small_indices[3] = {2, 3, 7};
    std::memcpy(bytes.data() + 312, &small_indices[0], 3);
    const float times[2] = {0.0f, 1.0f};
    std::memcpy(bytes.data() + 320, &times[0], sizeof(times));
    const float translations[6] = {0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 3.0f};
    std::memcpy(bytes.data() + 328, &translations[0], sizeof(translations));
    return bytes;
}

[[nodiscard]] auto make_json(const std::string& buffer_member) -> std::string
{
    return std::string{R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"name": "Strip", "mesh": 0}],
  "meshes": [{"name": "Strip", "primitives": [
    {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3},
    {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 4},
    {"attributes": {"POSITION": 0, "COLOR_0": 5}, "indices": 6}
  ]}],
  "animations": [{"name": "Move", "samplers": [{"input": 7, "output": 8}], "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}]}],
  "buffers": [{)"} + buffer_member + R"("byteLength": 352}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0,   "byteLength": 256, "byteStride": 32},
    {"buffer": 0, "byteOffset": 256, "byteLength": 24},
    {"buffer": 0, "byteOffset": 280, "byteLength": 32},
    {"buffer": 0, "byteOffset": 312, "byteLength": 3},
    {"buffer": 0, "byteOffset": 320, "byteLength": 32}
  ],
  "accessors": [
    {"bufferView": 0, "byteOffset": 0,  "componentType": 5126, "count": 8, "type": "VEC3", "min": [0, 0, 0], "max": [1.5, 1.25, 0.875]},
    {"bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 8, "type": "VEC3"},
    {"bufferView": 0, "byteOffset": 24, "componentType": 5126, "count": 8, "type": "VEC2"},
    {"bufferView": 1, "byteOffset": 0,  "componentType": 5123, "count": 6, "type": "SCALAR"},
    {"bufferView": 1, "byteOffset": 12, "componentType": 5123, "count": 6, "type": "SCALAR"},
    {"bufferView": 2, "byteOffset": 0,  "componentType": 5121, "normalized": true, "count": 8, "type": "VEC4"},
    {"bufferView": 3, "byteOffset": 0,  "componentType": 5121, "count": 3, "type": "SCALAR"},
    {"bufferView": 4, "byteOffset": 0,  "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [1]},
    {"bufferView": 4, "byteOffset": 8,  "componentType": 5126, "count": 2, "type": "VEC3"}
  ]
})";
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes)
{
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    ASSERT_TRUE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(stream.good());
}

void append_u32(std::vector<std::byte>& out, const uint32_t value)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xffu));
    }
}

[[nodiscard]] auto write_gltf_with_bin(const std::string& stem) -> std::filesystem::path
{
    const std::string bin_name = stem + ".bin";
    write_bytes(erhe_gltf_test::process_temporary_directory() / bin_name, make_buffer());
    return erhe_gltf_test::write_temporary_file(stem + ".gltf", make_json("\"uri\": \"" + bin_name + "\", "));
}

[[nodiscard]] auto make_glb() -> std::vector<std::byte>;

[[nodiscard]] auto write_glb(const std::string& stem) -> std::filesystem::path
{
    const std::filesystem::path path = erhe_gltf_test::process_temporary_directory() / (stem + ".glb");
    write_bytes(path, make_glb());
    return path;
}

[[nodiscard]] auto make_glb() -> std::vector<std::byte>
{
    std::string json = make_json("");
    while ((json.size() % 4) != 0) {
        json.push_back(' ');
    }
    std::vector<std::byte> bin = make_buffer();
    while ((bin.size() % 4) != 0) {
        bin.push_back(std::byte{0});
    }
    std::vector<std::byte> glb;
    append_u32(glb, 0x46546C67u);
    append_u32(glb, 2);
    append_u32(glb, static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    append_u32(glb, static_cast<uint32_t>(json.size()));
    append_u32(glb, 0x4E4F534Au);
    for (const char c : json) {
        glb.push_back(static_cast<std::byte>(c));
    }
    append_u32(glb, static_cast<uint32_t>(bin.size()));
    append_u32(glb, 0x004E4942u);
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

// The same content with the buffer in a base64 data URI: a resident buffer,
// parsed through the asset.
[[nodiscard]] auto write_gltf_with_data_uri(const std::string& stem) -> std::filesystem::path
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const std::vector<std::byte> bytes = make_buffer();
    std::string base64;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t b0 = static_cast<uint32_t>(bytes[i]);
        const uint32_t b1 = ((i + 1) < bytes.size()) ? static_cast<uint32_t>(bytes[i + 1]) : 0u;
        const uint32_t b2 = ((i + 2) < bytes.size()) ? static_cast<uint32_t>(bytes[i + 2]) : 0u;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        base64.push_back(alphabet[(triple >> 18) & 63u]);
        base64.push_back(alphabet[(triple >> 12) & 63u]);
        base64.push_back(((i + 1) < bytes.size()) ? alphabet[(triple >> 6) & 63u] : '=');
        base64.push_back(((i + 2) < bytes.size()) ? alphabet[triple & 63u] : '=');
    }
    return erhe_gltf_test::write_temporary_file(stem + ".gltf", make_json("\"uri\": \"data:application/octet-stream;base64," + base64 + "\", "));
}

[[nodiscard]] auto parse_recording(const std::filesystem::path& path, erhe::scene::Scene& scene) -> erhe::gltf::Gltf_data
{
    tf::Executor executor{1};
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    return erhe::gltf::parse_gltf(
        erhe::gltf::Gltf_parse_arguments{
            .executor             = executor,
            .root_node            = root_node,
            .path                 = path,
            .parallel             = false,
            .triangle_soup_reload = erhe::gltf::Triangle_soup_reload::record
        }
    );
}

void expect_bit_identical(const Triangle_soup& lhs, const Triangle_soup& rhs)
{
    EXPECT_EQ(lhs.vertex_data, rhs.vertex_data);
    EXPECT_EQ(lhs.index_data, rhs.index_data);
    EXPECT_EQ(lhs.primitive_type, rhs.primitive_type);
    ASSERT_EQ(lhs.vertex_format.streams.size(), rhs.vertex_format.streams.size());
    for (std::size_t s = 0; s < lhs.vertex_format.streams.size(); ++s) {
        const erhe::dataformat::Vertex_stream& a = lhs.vertex_format.streams[s];
        const erhe::dataformat::Vertex_stream& b = rhs.vertex_format.streams[s];
        EXPECT_EQ(a.stride, b.stride);
        ASSERT_EQ(a.attributes.size(), b.attributes.size());
        for (std::size_t i = 0; i < a.attributes.size(); ++i) {
            EXPECT_EQ(a.attributes[i].format,      b.attributes[i].format);
            EXPECT_EQ(a.attributes[i].usage_type,  b.attributes[i].usage_type);
            EXPECT_EQ(a.attributes[i].usage_index, b.attributes[i].usage_index);
            EXPECT_EQ(a.attributes[i].offset,      b.attributes[i].offset);
        }
    }
}

// Parses, releases every primitive's soup and reloads it; the reload must
// equal a copy taken before the release.
void check_reload_bit_identical(const std::filesystem::path& path)
{
    erhe::scene::Scene          scene{"test", nullptr};
    const erhe::gltf::Gltf_data data = parse_recording(path, scene);
    ASSERT_EQ(data.meshes.size(), 1u);
    const std::vector<erhe::scene::Mesh_primitive>& primitives = data.meshes[0]->get_primitives();
    ASSERT_EQ(primitives.size(), 3u);
    for (const erhe::scene::Mesh_primitive& mesh_primitive : primitives) {
        ASSERT_TRUE(mesh_primitive.primitive);
        erhe::primitive::Primitive& primitive = *mesh_primitive.primitive;
        ASSERT_TRUE(primitive.render_shape);
        ASSERT_TRUE(primitive.render_shape->get_triangle_soup_source());
        const std::shared_ptr<Triangle_soup> parsed = primitive.render_shape->get_triangle_soup();
        ASSERT_TRUE(parsed);
        const Triangle_soup original = *parsed;

        ASSERT_TRUE(primitive.release_triangle_soup());
        EXPECT_TRUE(primitive.render_shape->is_triangle_soup_released());
        const std::shared_ptr<Triangle_soup> reloaded = primitive.render_shape->get_triangle_soup();
        ASSERT_TRUE(reloaded) << path.string();
        EXPECT_NE(reloaded.get(), parsed.get());
        expect_bit_identical(original, *reloaded);
    }
}

TEST(Gltf_soup_reload, gltf_with_external_bin_is_bit_identical)
{
    check_reload_bit_identical(write_gltf_with_bin("reload_external"));
}

TEST(Gltf_soup_reload, glb_is_bit_identical)
{
    check_reload_bit_identical(write_glb("reload_glb"));
}

TEST(Gltf_soup_reload, changed_file_is_refused)
{
    const std::filesystem::path path = write_gltf_with_bin("reload_changed");
    const std::filesystem::path bin_path = erhe_gltf_test::process_temporary_directory() / "reload_changed.bin";

    erhe::scene::Scene          scene{"test", nullptr};
    const erhe::gltf::Gltf_data data = parse_recording(path, scene);
    ASSERT_EQ(data.meshes.size(), 1u);
    erhe::primitive::Primitive& primitive = *data.meshes[0]->get_primitives().front().primitive;
    ASSERT_TRUE(primitive.release_triangle_soup());

    // Same size, different bytes, later modification time.
    std::vector<std::byte> changed = make_buffer();
    changed[0] = std::byte{0x7f};
    const std::filesystem::file_time_type old_time = std::filesystem::last_write_time(bin_path);
    write_bytes(bin_path, changed);
    std::filesystem::last_write_time(bin_path, old_time + std::chrono::seconds{2});

    const erhe::primitive::Triangle_soup_reload_statistics before = erhe::primitive::get_triangle_soup_reload_statistics();
    EXPECT_FALSE(primitive.render_shape->get_triangle_soup());
    EXPECT_EQ(erhe::primitive::get_triangle_soup_reload_statistics().reload_failure_count - before.reload_failure_count, 1u);
}

[[nodiscard]] auto parse_memory(const std::vector<std::byte>& glb, erhe::scene::Scene& scene) -> erhe::gltf::Gltf_data
{
    tf::Executor executor{1};
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    return erhe::gltf::parse_gltf(
        erhe::gltf::Gltf_parse_arguments{
            .executor = executor,
            .root_node = root_node,
            .path      = "memory.glb",
            .parallel  = false,
            .glb_data  = std::span<const std::byte>{glb.data(), glb.size()}
        }
    );
}

// The parse of a file reads the soups by range and the animation view whole;
// the result equals a parse of the same content from memory or from a data
// URI, where every buffer is resident.
void expect_same_content(const erhe::gltf::Gltf_data& lhs, const erhe::gltf::Gltf_data& rhs)
{
    ASSERT_EQ(lhs.meshes.size(), 1u);
    ASSERT_EQ(rhs.meshes.size(), 1u);
    const std::vector<erhe::scene::Mesh_primitive>& lhs_primitives = lhs.meshes[0]->get_primitives();
    const std::vector<erhe::scene::Mesh_primitive>& rhs_primitives = rhs.meshes[0]->get_primitives();
    ASSERT_EQ(lhs_primitives.size(), 3u);
    ASSERT_EQ(rhs_primitives.size(), 3u);
    for (std::size_t i = 0; i < lhs_primitives.size(); ++i) {
        const std::shared_ptr<Triangle_soup> a = lhs_primitives[i].primitive->render_shape->get_triangle_soup();
        const std::shared_ptr<Triangle_soup> b = rhs_primitives[i].primitive->render_shape->get_triangle_soup();
        ASSERT_TRUE(a);
        ASSERT_TRUE(b);
        expect_bit_identical(*a, *b);
    }
    ASSERT_EQ(lhs.animations.size(), 1u);
    ASSERT_EQ(rhs.animations.size(), 1u);
    ASSERT_EQ(lhs.animations[0]->samplers.size(), 1u);
    ASSERT_EQ(rhs.animations[0]->samplers.size(), 1u);
    EXPECT_EQ(lhs.animations[0]->samplers[0].timestamps, rhs.animations[0]->samplers[0].timestamps);
    EXPECT_EQ(lhs.animations[0]->samplers[0].data,       rhs.animations[0]->samplers[0].data);
    EXPECT_EQ(lhs.animations[0]->samplers[0].timestamps, (std::vector<float>{0.0f, 1.0f}));
}

TEST(Gltf_soup_reload, glb_file_parse_equals_memory_parse)
{
    erhe::scene::Scene          file_scene{"file", nullptr};
    const erhe::gltf::Gltf_data file_data = erhe_gltf_test::parse_file(write_glb("by_range_glb"), file_scene);
    erhe::scene::Scene          memory_scene{"memory", nullptr};
    const std::vector<std::byte> glb = make_glb();
    const erhe::gltf::Gltf_data memory_data = parse_memory(glb, memory_scene);
    expect_same_content(file_data, memory_data);
}

TEST(Gltf_soup_reload, external_bin_parse_equals_data_uri_parse)
{
    erhe::scene::Scene          file_scene{"file", nullptr};
    const erhe::gltf::Gltf_data file_data = erhe_gltf_test::parse_file(write_gltf_with_bin("by_range_bin"), file_scene);
    erhe::scene::Scene          uri_scene{"uri", nullptr};
    const erhe::gltf::Gltf_data uri_data = erhe_gltf_test::parse_file(write_gltf_with_data_uri("by_range_uri"), uri_scene);
    expect_same_content(file_data, uri_data);
}

TEST(Gltf_soup_reload, no_source_without_record)
{
    const std::filesystem::path path = write_glb("reload_none");
    erhe::scene::Scene          scene{"test", nullptr};
    const erhe::gltf::Gltf_data data = erhe_gltf_test::parse_file(path, scene);
    ASSERT_EQ(data.meshes.size(), 1u);
    erhe::primitive::Primitive& primitive = *data.meshes[0]->get_primitives().front().primitive;
    EXPECT_FALSE(primitive.render_shape->get_triangle_soup_source());
    EXPECT_FALSE(primitive.release_triangle_soup());
    EXPECT_TRUE(primitive.render_shape->get_triangle_soup());
}

} // anonymous namespace
