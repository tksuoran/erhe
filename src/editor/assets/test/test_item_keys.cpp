#include "assets/item_keys.hpp"

#include "erhe_asset_store/key.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_primitive/triangle_soup.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <span>
#include <cstring>
#include <string>
#include <vector>

namespace {

using erhe::asset_store::Key;
using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute;
using erhe::dataformat::Vertex_attribute_usage;

[[nodiscard]] auto make_key(const std::uint8_t seed) -> Key
{
    Key key{};
    for (std::size_t i = 0; i < Key::byte_count; ++i) {
        key.bytes[i] = static_cast<std::uint8_t>(seed + i);
    }
    return key;
}

// Two attributes with two bytes of padding between them (uv8 at 0, position at 4).
[[nodiscard]] auto make_soup() -> erhe::primitive::Triangle_soup
{
    erhe::primitive::Triangle_soup soup;
    soup.vertex_format = erhe::dataformat::Vertex_format{
        erhe::dataformat::Vertex_stream{
            0,
            {
                Vertex_attribute{.format = Format::format_8_vec2_unorm,   .usage_type = Vertex_attribute_usage::tex_coord, .usage_index = 0},
                Vertex_attribute{.format = Format::format_32_vec3_float,  .usage_type = Vertex_attribute_usage::position,  .usage_index = 0}
            }
        }
    };
    const std::size_t stride = soup.vertex_format.streams.front().stride;
    soup.vertex_data.resize(3 * stride, 0);
    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
        const float position[3] = {static_cast<float>(vertex), 1.0f, 2.0f};
        std::memcpy(soup.vertex_data.data() + (vertex * stride) + 4, position, sizeof(position));
        soup.vertex_data[vertex * stride] = static_cast<std::uint8_t>(10 + vertex);
    }
    soup.index_data = {0, 1, 2};
    return soup;
}

[[nodiscard]] auto make_inputs() -> editor::Local_mesh_key_inputs
{
    return editor::Local_mesh_key_inputs{
        .content_hash     = make_key(1),
        .lod_settings     = erhe::primitive::Lod_settings{},
        .optimize_options = erhe::primitive::Mesh_optimize_options{},
        .stamp            = "layout=x"
    };
}

[[nodiscard]] auto as_bytes(const std::string& text) -> std::span<const std::byte>
{
    return std::as_bytes(std::span<const char>{text.data(), text.size()});
}

[[nodiscard]] auto make_glb(const std::string& json) -> std::string
{
    std::string padded = json;
    while ((padded.size() % 4) != 0) {
        padded.push_back(' ');
    }
    const auto u32 = [](std::string& out, const std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) {
            out.push_back(static_cast<char>((value >> (8u * i)) & 0xffu));
        }
    };
    std::string glb;
    u32(glb, 0x46546c67u);
    u32(glb, 2u);
    u32(glb, static_cast<std::uint32_t>(20 + padded.size()));
    u32(glb, static_cast<std::uint32_t>(padded.size()));
    u32(glb, 0x4e4f534au);
    glb += padded;
    return glb;
}

} // anonymous namespace

TEST(Item_keys, item_uri_round_trips)
{
    const Key key = make_key(7);
    const std::string uri = editor::make_item_uri(key);
    EXPECT_EQ(uri, std::string{"erhe-item:"} + key.to_hex());
    EXPECT_EQ(editor::parse_item_uri(uri), key);
    EXPECT_FALSE(editor::parse_item_uri("erhe-item:").has_value());
    EXPECT_FALSE(editor::parse_item_uri("erhe-item:" + key.to_hex() + "0").has_value());
    EXPECT_FALSE(editor::parse_item_uri("file:" + key.to_hex()).has_value());
    EXPECT_FALSE(editor::parse_item_uri("erhe-item:zz" + key.to_hex().substr(2)).has_value());
    EXPECT_FALSE(editor::parse_item_uri("model.bin").has_value());
}

TEST(Item_keys, soup_content_hash_covers_indices_attributes_and_descriptors)
{
    const erhe::primitive::Triangle_soup soup = make_soup();
    const Key reference = editor::compute_soup_content_hash(soup);
    EXPECT_EQ(editor::compute_soup_content_hash(make_soup()), reference);

    erhe::primitive::Triangle_soup padding = make_soup();
    padding.vertex_data[2] = 0x55; // padding byte of vertex 0
    EXPECT_EQ(editor::compute_soup_content_hash(padding), reference);

    erhe::primitive::Triangle_soup index = make_soup();
    index.index_data = {0, 2, 1};
    EXPECT_NE(editor::compute_soup_content_hash(index), reference);

    erhe::primitive::Triangle_soup texcoord = make_soup();
    texcoord.vertex_data[0] = 99;
    EXPECT_NE(editor::compute_soup_content_hash(texcoord), reference);

    erhe::primitive::Triangle_soup position = make_soup();
    position.vertex_data[4 + 1] ^= 0x01u;
    EXPECT_NE(editor::compute_soup_content_hash(position), reference);

    erhe::primitive::Triangle_soup usage_index = make_soup();
    usage_index.vertex_format.streams.front().attributes.front().usage_index = 1;
    EXPECT_NE(editor::compute_soup_content_hash(usage_index), reference);

    erhe::primitive::Triangle_soup format = make_soup();
    format.vertex_format.streams.front().attributes.front().format = Format::format_8_vec2_uint;
    EXPECT_NE(editor::compute_soup_content_hash(format), reference);

    erhe::primitive::Triangle_soup primitive_type = make_soup();
    primitive_type.primitive_type = erhe::primitive::Primitive_type::points;
    EXPECT_NE(editor::compute_soup_content_hash(primitive_type), reference);
}

// The content hash moved into erhe::primitive (R3.5) hashes the byte stream the
// keys of earlier exports were made from: fed here field by field through
// erhe::asset_store::Content_hasher, as the editor's own function fed it.
TEST(Item_keys, soup_content_hash_keeps_its_byte_stream)
{
    const erhe::primitive::Triangle_soup soup = make_soup();
    erhe::asset_store::Content_hasher hasher;
    const auto add_u64 = [&hasher](const std::uint64_t value) {
        std::array<std::byte, 8> bytes{};
        for (std::size_t i = 0; i < 8; ++i) {
            bytes[i] = static_cast<std::byte>((value >> (8u * i)) & 0xffu);
        }
        hasher.update(bytes);
    };
    add_u64(static_cast<std::uint64_t>(soup.primitive_type));
    add_u64(soup.index_data.size());
    hasher.update(std::as_bytes(std::span<const std::uint32_t>{soup.index_data}));
    add_u64(soup.vertex_format.streams.size());
    const std::size_t vertex_count = soup.get_vertex_count();
    add_u64(vertex_count);
    const erhe::dataformat::Vertex_stream& stream = soup.vertex_format.streams.front();
    add_u64(stream.attributes.size());
    for (const Vertex_attribute& attribute : stream.attributes) {
        add_u64(static_cast<std::uint64_t>(attribute.usage_type));
        add_u64(attribute.usage_index);
        add_u64(static_cast<std::uint64_t>(attribute.format));
        const std::size_t size = erhe::dataformat::get_format_size_bytes(attribute.format);
        for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
            hasher.update(std::as_bytes(std::span<const std::uint8_t>{soup.vertex_data}.subspan((vertex * stream.stride) + attribute.offset, size)));
        }
    }
    EXPECT_EQ(editor::compute_soup_content_hash(soup), hasher.finish());
    EXPECT_EQ(editor::to_key(erhe::primitive::compute_triangle_soup_content_hash(soup)), hasher.finish());
}

TEST(Item_keys, local_mesh_key_covers_every_input)
{
    const Key reference = editor::make_local_mesh_key(make_inputs());
    EXPECT_EQ(editor::make_local_mesh_key(make_inputs()), reference);

    std::vector<editor::Local_mesh_key_inputs> variants;
    { editor::Local_mesh_key_inputs v = make_inputs(); v.source = editor::Local_mesh_source::derived_triangles;   variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.content_hash = make_key(2);                                variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.stamp = "layout=0";                                       variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.lod_settings.reset();                                     variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.lod_settings->level_count = 3;                            variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.lod_settings->ratio = 0.25f;                              variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.lod_settings->attributes = erhe::primitive::Lod_attributes::positions_only; variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.lod_settings->normal_weight = 2.0f;                       variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.lod_settings->color_weight = 2.0f;                        variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.lod_settings->border = erhe::primitive::Lod_border::free;    variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.lod_settings->seams = erhe::primitive::Lod_seams::permissive;      variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.lod_settings->fallback = erhe::primitive::Lod_fallback::sloppy; variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.lod_settings->normal_merge_angle = 30.0f;                 variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.optimize_options.weld = false;                            variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.optimize_options.vertex_cache = false;                    variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.optimize_options.overdraw = false;                        variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.optimize_options.vertex_fetch = false;                    variants.push_back(v); }
    { editor::Local_mesh_key_inputs v = make_inputs(); v.optimize_options.overdraw_threshold = 1.5f;               variants.push_back(v); }
    std::vector<Key> keys{reference};
    for (const editor::Local_mesh_key_inputs& variant : variants) {
        const Key key = editor::make_local_mesh_key(variant);
        for (const Key& other : keys) {
            EXPECT_NE(key, other);
        }
        keys.push_back(key);
    }
}

TEST(Item_keys, stamp_and_scene_key)
{
    EXPECT_EQ(editor::make_local_mesh_stamp("s0=8:position.0=32_vec3_float@0"), editor::make_local_mesh_stamp("s0=8:position.0=32_vec3_float@0"));
    EXPECT_NE(editor::make_local_mesh_stamp("s0=8:position.0=32_vec3_float@0"), editor::make_local_mesh_stamp("s0=12:position.0=32_vec3_float@0"));

    const std::string a = "scene a";
    const std::string b = "scene b";
    EXPECT_EQ(editor::make_scene_item_key("pc", "glb;textures=source", as_bytes(a)), editor::make_scene_item_key("pc", "glb;textures=source", as_bytes(std::string{"scene a"})));
    EXPECT_NE(editor::make_scene_item_key("pc", "glb;textures=source", as_bytes(a)), editor::make_scene_item_key("pc", "glb;textures=source", as_bytes(b)));
    // The kind is part of the key: a Scene key is never the bare content hash.
    EXPECT_NE(editor::make_scene_item_key("pc", "glb;textures=source", as_bytes(a)), erhe::asset_store::Content_hasher::hash(as_bytes(a)));
}

// A BVH item is keyed by its mesh item and the BVH library stamp; it never shares a key with its mesh item.
TEST(Item_keys, raytrace_bvh_key_follows_mesh_key_and_stamp)
{
    const Key mesh_a = editor::make_local_mesh_key(make_inputs());
    editor::Local_mesh_key_inputs other = make_inputs();
    other.content_hash = make_key(3);
    const Key mesh_b = editor::make_local_mesh_key(other);
    const std::string stamp_1 = editor::make_raytrace_bvh_stamp("backend", "bvh 1");
    const std::string stamp_2 = editor::make_raytrace_bvh_stamp("backend", "bvh 2");
    const Key reference = editor::make_raytrace_bvh_key(mesh_a, stamp_1);
    EXPECT_EQ(editor::make_raytrace_bvh_key(mesh_a, stamp_1), reference);
    EXPECT_NE(editor::make_raytrace_bvh_key(mesh_b, stamp_1), reference);
    EXPECT_NE(editor::make_raytrace_bvh_key(mesh_a, stamp_2), reference);
    EXPECT_NE(reference, mesh_a);
    // The mesh key does not depend on the BVH library: mesh items are shared across BVH library versions.
    EXPECT_EQ(editor::make_local_mesh_key(make_inputs()), mesh_a);
}

// A World-space mesh key covers the mesh key, every bit of the transform and the stamp (R4.7).
TEST(Item_keys, world_mesh_key_covers_transform_bits)
{
    const Key mesh_a = editor::make_local_mesh_key(make_inputs());
    editor::Local_mesh_key_inputs other = make_inputs();
    other.content_hash = make_key(3);
    const Key mesh_b = editor::make_local_mesh_key(other);
    glm::mat4 transform{1.0f};
    transform[3] = glm::vec4{1.5f, -2.0f, 0.25f, 1.0f};
    const editor::World_mesh_key_inputs inputs{.local_mesh_key = mesh_a, .world_from_node = transform, .stamp = "layout=x"};
    const Key reference = editor::make_world_mesh_key(inputs);
    EXPECT_EQ(editor::make_world_mesh_key(inputs), reference);
    EXPECT_NE(reference, mesh_a);
    editor::World_mesh_key_inputs changed = inputs;
    changed.local_mesh_key = mesh_b;
    EXPECT_NE(editor::make_world_mesh_key(changed), reference);
    changed = inputs;
    changed.stamp = "layout=y";
    EXPECT_NE(editor::make_world_mesh_key(changed), reference);
    // Each of the 16 floats, moved by one ulp, asks for another key.
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            changed = inputs;
            float& value = changed.world_from_node[column][row];
            value = std::nextafter(value, 2.0f);
            EXPECT_NE(editor::make_world_mesh_key(changed), reference) << column << " " << row;
        }
    }
    // -0.0 and 0.0 differ in their bits.
    changed = inputs;
    changed.world_from_node[0][1] = -0.0f;
    EXPECT_NE(editor::make_world_mesh_key(changed), reference);
    EXPECT_EQ(editor::format_transform_hexfloat(glm::mat4{1.0f}).substr(0, 15), "0x1p+0 0x0p+0 0");
}

// A Texture key covers the source content, the texture properties the transcode reads, the supercompression and the stamp.
TEST(Item_keys, texture_key_separates_every_input)
{
    const auto make_texture_inputs = []() -> editor::Texture_key_inputs {
        return editor::Texture_key_inputs{
            .content_hash      = make_key(7),
            .transfer_function = erhe::graphics::Texture_transfer_function::srgb,
            .normal_encoding   = erhe::graphics::Texture_normal_encoding::none,
            .supercompression  = erhe::graphics::Ktx2_supercompression::zstd,
            .stamp             = editor::make_texture_stamp(erhe::graphics::Ktx2_block_target::bc)
        };
    };
    const Key reference = editor::make_texture_key(make_texture_inputs());
    EXPECT_EQ(editor::make_texture_key(make_texture_inputs()), reference);
    std::vector<editor::Texture_key_inputs> variants;
    { editor::Texture_key_inputs v = make_texture_inputs(); v.content_hash = make_key(8);                                              variants.push_back(v); }
    { editor::Texture_key_inputs v = make_texture_inputs(); v.transfer_function = erhe::graphics::Texture_transfer_function::linear;   variants.push_back(v); }
    { editor::Texture_key_inputs v = make_texture_inputs(); v.normal_encoding = erhe::graphics::Texture_normal_encoding::three_component; variants.push_back(v); }
    { editor::Texture_key_inputs v = make_texture_inputs(); v.normal_encoding = erhe::graphics::Texture_normal_encoding::two_component;   variants.push_back(v); }
    { editor::Texture_key_inputs v = make_texture_inputs(); v.supercompression = erhe::graphics::Ktx2_supercompression::none;          variants.push_back(v); }
    { editor::Texture_key_inputs v = make_texture_inputs(); v.stamp = editor::make_texture_stamp(erhe::graphics::Ktx2_block_target::astc_4x4); variants.push_back(v); }
    std::vector<Key> keys{reference, make_key(7)};
    for (const editor::Texture_key_inputs& variant : variants) {
        const Key key = editor::make_texture_key(variant);
        for (const Key& other : keys) {
            EXPECT_NE(key, other);
        }
        keys.push_back(key);
    }
    EXPECT_EQ(editor::make_texture_stamp(erhe::graphics::Ktx2_block_target::bc),       "ktx2;blocks=bc");
    EXPECT_EQ(editor::make_texture_stamp(erhe::graphics::Ktx2_block_target::astc_4x4), "ktx2;blocks=astc_4x4");
}

// R7.5: a PNG / JPEG image's Texture key names its texture_encoding; a Basis source's key is the step 6b key.
TEST(Item_keys, texture_key_separates_texture_item_sources)
{
    const editor::Texture_key_inputs basis_source{
        .source       = editor::Texture_item_source::basis_source,
        .content_hash = make_key(7),
        .stamp        = editor::make_texture_stamp(erhe::graphics::Ktx2_block_target::bc)
    };
    editor::Texture_key_inputs from_basis_item = basis_source;
    from_basis_item.source = editor::Texture_item_source::basis_texture_item;
    editor::Texture_key_inputs source_bytes = basis_source;
    source_bytes.source = editor::Texture_item_source::source_bytes;
    source_bytes.stamp  = editor::make_source_texture_stamp();
    const Key a = editor::make_texture_key(basis_source);
    const Key b = editor::make_texture_key(from_basis_item);
    const Key c = editor::make_texture_key(source_bytes);
    EXPECT_NE(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(b, c);
    // The source bytes as they are: no transcode input is a key input.
    editor::Texture_key_inputs linear = source_bytes;
    linear.transfer_function = erhe::graphics::Texture_transfer_function::linear;
    linear.supercompression  = erhe::graphics::Ktx2_supercompression::none;
    EXPECT_EQ(editor::make_texture_key(linear), c);
    EXPECT_EQ(editor::make_source_texture_stamp(), "source");
}

// A Basis texture key covers the source content, every texture property, the encoder settings and the stamp.
TEST(Item_keys, basis_texture_key_separates_every_input)
{
    const auto make_inputs = []() -> editor::Basis_texture_key_inputs {
        return editor::Basis_texture_key_inputs{
            .content_hash = make_key(7),
            .properties   = erhe::graphics::Texture_properties{},
            .settings     = erhe::graphics::Basis_encode_settings{},
            .stamp        = editor::make_basis_texture_stamp("2.50")
        };
    };
    const Key reference = editor::make_basis_texture_key(make_inputs());
    EXPECT_EQ(editor::make_basis_texture_key(make_inputs()), reference);
    std::vector<editor::Basis_texture_key_inputs> variants;
    { editor::Basis_texture_key_inputs v = make_inputs(); v.content_hash = make_key(8);                                                          variants.push_back(v); }
    { editor::Basis_texture_key_inputs v = make_inputs(); v.properties.transfer_function = erhe::graphics::Texture_transfer_function::linear;     variants.push_back(v); }
    { editor::Basis_texture_key_inputs v = make_inputs(); v.properties.normal_encoding = erhe::graphics::Texture_normal_encoding::two_component;  variants.push_back(v); }
    { editor::Basis_texture_key_inputs v = make_inputs(); v.properties.generate_mipmaps = false;                                                   variants.push_back(v); }
    { editor::Basis_texture_key_inputs v = make_inputs(); v.properties.wrap_u = erhe::graphics::Sampler_address_mode::clamp_to_edge;              variants.push_back(v); }
    { editor::Basis_texture_key_inputs v = make_inputs(); v.properties.wrap_v = erhe::graphics::Sampler_address_mode::mirrored_repeat;            variants.push_back(v); }
    { editor::Basis_texture_key_inputs v = make_inputs(); v.settings.uastc_level = 3;                                                              variants.push_back(v); }
    { editor::Basis_texture_key_inputs v = make_inputs(); v.settings.zstd_level = 9;                                                               variants.push_back(v); }
    { editor::Basis_texture_key_inputs v = make_inputs(); v.stamp = editor::make_basis_texture_stamp("2.60");                                      variants.push_back(v); }
    std::vector<Key> keys{reference, make_key(7)};
    for (const editor::Basis_texture_key_inputs& variant : variants) {
        const Key key = editor::make_basis_texture_key(variant);
        for (const Key& other : keys) {
            EXPECT_NE(key, other);
        }
        keys.push_back(key);
    }
    EXPECT_EQ(editor::make_basis_texture_stamp("2.50"), "ktx2;basis=uastc_ldr_4x4;basisu=2.50");
}

TEST(Item_keys, collect_scene_item_keys_reads_item_buffers)
{
    const Key k1 = make_key(1);
    const Key k2 = make_key(50);
    const std::string json =
        "{\"asset\":{\"version\":\"2.0\"},\"buffers\":["
        "{\"byteLength\":16},"
        "{\"uri\":\"" + editor::make_item_uri(k2) + "\",\"byteLength\":8},"
        "{\"uri\":\"external.bin\",\"byteLength\":8},"
        "{\"uri\":\"" + editor::make_item_uri(k1) + "\",\"byteLength\":8},"
        "{\"uri\":\"" + editor::make_item_uri(k2) + "\",\"byteLength\":8}"
        "]}";
    const std::vector<Key> expected = (k1 < k2) ? std::vector<Key>{k1, k2} : std::vector<Key>{k2, k1};

    const std::string glb = make_glb(json);
    const std::optional<std::vector<Key>> from_glb = editor::collect_scene_item_keys(as_bytes(glb));
    ASSERT_TRUE(from_glb.has_value());
    EXPECT_EQ(from_glb.value(), expected);

    const std::optional<std::vector<Key>> from_json = editor::collect_scene_item_keys(as_bytes(json));
    ASSERT_TRUE(from_json.has_value());
    EXPECT_EQ(from_json.value(), expected);

    const std::optional<std::vector<Key>> none = editor::collect_scene_item_keys(as_bytes(std::string{"{\"asset\":{\"version\":\"2.0\"}}"}));
    ASSERT_TRUE(none.has_value());
    EXPECT_TRUE(none->empty());

    EXPECT_FALSE(editor::collect_scene_item_keys(as_bytes(std::string{"{not json"})).has_value());
    std::string truncated = glb.substr(0, 24);
    EXPECT_FALSE(editor::collect_scene_item_keys(as_bytes(truncated)).has_value());
}
