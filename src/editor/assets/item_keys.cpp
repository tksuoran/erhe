#include "assets/item_keys.hpp"

#include "erhe_asset_store/item_kind.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_gltf/gltf_fastgltf.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_scene/pvs_database.hpp"

#include <nlohmann/json.hpp>
#include <simdjson.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace editor {

namespace {

[[nodiscard]] auto read_u32_le(std::span<const std::byte> bytes, const std::size_t offset) -> std::uint32_t
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= (static_cast<std::uint32_t>(bytes[offset + i]) << (8u * i));
    }
    return value;
}

// The JSON text of a GLB (its first chunk) or of a glTF JSON document.
[[nodiscard]] auto get_gltf_json_text(std::span<const std::byte> document) -> std::optional<std::string_view>
{
    constexpr std::uint32_t glb_magic      = 0x46546c67u; // "glTF"
    constexpr std::uint32_t json_chunk_tag = 0x4e4f534au; // "JSON"
    if ((document.size() >= 12) && (read_u32_le(document, 0) == glb_magic)) {
        if (document.size() < 20) {
            return std::nullopt;
        }
        const std::uint32_t chunk_length = read_u32_le(document, 12);
        const std::uint32_t chunk_type   = read_u32_le(document, 16);
        if ((chunk_type != json_chunk_tag) || ((static_cast<std::size_t>(chunk_length) + 20u) > document.size())) {
            return std::nullopt;
        }
        return std::string_view{reinterpret_cast<const char*>(document.data() + 20), chunk_length};
    }
    return std::string_view{reinterpret_cast<const char*>(document.data()), document.size()};
}

} // anonymous namespace

auto make_item_uri(const erhe::asset_store::Key& key) -> std::string
{
    return std::string{c_item_uri_prefix} + key.to_hex();
}

auto parse_item_uri(const std::string_view uri) -> std::optional<erhe::asset_store::Key>
{
    if (!uri.starts_with(c_item_uri_prefix)) {
        return std::nullopt;
    }
    const std::string_view hex = uri.substr(c_item_uri_prefix.size());
    if (hex.size() != (2 * erhe::asset_store::Key::byte_count)) {
        return std::nullopt;
    }
    return erhe::asset_store::Key::from_hex(hex);
}

auto to_key(const erhe::hash::Content_hash& content_hash) -> erhe::asset_store::Key
{
    static_assert(erhe::hash::Content_hash::byte_count == erhe::asset_store::Key::byte_count);
    erhe::asset_store::Key key{};
    key.bytes = content_hash.bytes;
    return key;
}

auto compute_soup_content_hash(const erhe::primitive::Triangle_soup& soup) -> erhe::asset_store::Key
{
    return to_key(erhe::primitive::compute_triangle_soup_content_hash(soup));
}

auto make_shape_local_mesh_key(
    const Local_mesh_source                             source,
    const erhe::hash::Content_hash&                     content_hash,
    const std::optional<erhe::primitive::Lod_settings>& lod_settings,
    const std::string_view                              stamp
) -> erhe::asset_store::Key
{
    return make_local_mesh_key(Local_mesh_key_inputs{
        .source           = source,
        .content_hash     = to_key(content_hash),
        .lod_settings     = lod_settings,
        .optimize_options = erhe::primitive::Mesh_optimize_options{},
        .stamp            = std::string{stamp}
    });
}

auto make_local_mesh_key(const Local_mesh_key_inputs& inputs) -> erhe::asset_store::Key
{
    erhe::asset_store::Key_builder builder{erhe::asset_store::Item_kind::local_mesh};
    builder.add_code_version(c_local_mesh_code_version);
    builder.add_format_stamp(inputs.stamp);
    builder.add_content_hash(inputs.content_hash);
    // A soup key carries no source field.
    if (inputs.source == Local_mesh_source::derived_triangles) {
        builder.add_uint_setting("source.derived_triangles", 1u);
    }
    const erhe::primitive::Mesh_optimize_options& optimize = inputs.optimize_options;
    builder.add_uint_setting ("optimize.weld",               optimize.weld         ? 1u : 0u);
    builder.add_uint_setting ("optimize.vertex_cache",       optimize.vertex_cache ? 1u : 0u);
    builder.add_uint_setting ("optimize.overdraw",           optimize.overdraw     ? 1u : 0u);
    builder.add_uint_setting ("optimize.vertex_fetch",       optimize.vertex_fetch ? 1u : 0u);
    builder.add_float_setting("optimize.overdraw_threshold", optimize.overdraw_threshold);
    if (!inputs.lod_settings.has_value()) {
        builder.add_uint_setting("lod", 0u);
        return builder.finish();
    }
    const erhe::primitive::Lod_settings& lod = inputs.lod_settings.value();
    builder.add_uint_setting ("lod",                    1u);
    builder.add_uint_setting ("lod.level_count",        lod.level_count);
    builder.add_float_setting("lod.ratio",              lod.ratio);
    builder.add_uint_setting ("lod.attributes",         static_cast<std::uint64_t>(lod.attributes));
    builder.add_float_setting("lod.normal_weight",      lod.normal_weight);
    builder.add_float_setting("lod.color_weight",       lod.color_weight);
    builder.add_uint_setting ("lod.border",             static_cast<std::uint64_t>(lod.border));
    builder.add_uint_setting ("lod.seams",              static_cast<std::uint64_t>(lod.seams));
    builder.add_uint_setting ("lod.fallback",           static_cast<std::uint64_t>(lod.fallback));
    builder.add_float_setting("lod.normal_merge_angle", lod.normal_merge_angle);
    return builder.finish();
}

auto format_transform_hexfloat(const glm::mat4& transform) -> std::string
{
    std::string text;
    text.reserve(16 * 16);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!text.empty()) {
                text.push_back(' ');
            }
            // %a: exact bits (fmt writes zero with the minimum double exponent).
            std::array<char, 32> buffer{};
            const int length = std::snprintf(buffer.data(), buffer.size(), "%a", static_cast<double>(transform[column][row]));
            text.append(buffer.data(), static_cast<std::size_t>(std::max(length, 0)));
        }
    }
    return text;
}

auto make_world_mesh_key(const World_mesh_key_inputs& inputs) -> erhe::asset_store::Key
{
    erhe::asset_store::Key_builder builder{erhe::asset_store::Item_kind::world_mesh};
    builder.add_code_version(c_world_mesh_code_version);
    builder.add_format_stamp(inputs.stamp);
    builder.add_key(inputs.local_mesh_key);
    builder.add_text_setting("world_from_node", format_transform_hexfloat(inputs.world_from_node));
    return builder.finish();
}

auto make_local_mesh_stamp(const std::string_view vertex_layout_signature) -> std::string
{
    const erhe::asset_store::Key layout_hash = erhe::asset_store::Content_hasher::hash(
        std::as_bytes(std::span<const char>{vertex_layout_signature.data(), vertex_layout_signature.size()})
    );
    return "layout=" + layout_hash.to_hex();
}

auto make_raytrace_bvh_stamp(const std::string_view bvh_backend, const std::string_view bvh_library) -> std::string
{
    return "bvh=" + std::string{bvh_backend} + "/" + std::string{bvh_library};
}

auto make_raytrace_bvh_key(const erhe::asset_store::Key& mesh_key, const std::string_view stamp) -> erhe::asset_store::Key
{
    erhe::asset_store::Key_builder builder{erhe::asset_store::Item_kind::raytrace_bvh};
    builder.add_code_version(c_raytrace_bvh_code_version);
    builder.add_format_stamp(stamp);
    builder.add_key(mesh_key);
    return builder.finish();
}

auto make_texture_key(const Texture_key_inputs& inputs) -> erhe::asset_store::Key
{
    erhe::asset_store::Key_builder builder{erhe::asset_store::Item_kind::texture};
    builder.add_code_version(c_texture_code_version);
    builder.add_format_stamp(inputs.stamp);
    if (inputs.source == Texture_item_source::basis_texture_item) {
        builder.add_key(inputs.content_hash);
    } else {
        builder.add_content_hash(inputs.content_hash);
    }
    // A Basis Universal source's key carries no texture_encoding field (the keys of D8 step 6b).
    if (inputs.source == Texture_item_source::source_bytes) {
        builder.add_text_setting("texture_encoding", "source");
        return builder.finish();
    }
    if (inputs.source == Texture_item_source::basis_texture_item) {
        builder.add_text_setting("texture_encoding", "target");
    }
    builder.add_text_setting("transfer_function", erhe::graphics::c_str(inputs.transfer_function));
    builder.add_text_setting("normal_encoding",   erhe::graphics::c_str(inputs.normal_encoding));
    builder.add_uint_setting("supercompression",  static_cast<std::uint64_t>(inputs.supercompression));
    return builder.finish();
}

auto make_texture_stamp(const erhe::graphics::Ktx2_block_target block_target) -> std::string
{
    return (block_target == erhe::graphics::Ktx2_block_target::bc) ? "ktx2;blocks=bc" : "ktx2;blocks=astc_4x4";
}

auto make_source_texture_stamp() -> std::string
{
    return "source";
}

auto make_basis_texture_key(const Basis_texture_key_inputs& inputs) -> erhe::asset_store::Key
{
    erhe::asset_store::Key_builder builder{erhe::asset_store::Item_kind::basis_texture};
    builder.add_code_version(c_basis_texture_code_version);
    builder.add_format_stamp(inputs.stamp);
    builder.add_content_hash(inputs.content_hash);
    const erhe::graphics::Texture_properties& properties = inputs.properties;
    builder.add_text_setting("transfer_function", erhe::graphics::c_str(properties.transfer_function));
    builder.add_text_setting("normal_encoding",   erhe::graphics::c_str(properties.normal_encoding));
    builder.add_uint_setting("generate_mipmaps",  properties.generate_mipmaps ? 1u : 0u);
    builder.add_text_setting("wrap_u",            erhe::graphics::c_str_wrap(properties.wrap_u));
    builder.add_text_setting("wrap_v",            erhe::graphics::c_str_wrap(properties.wrap_v));
    // What encode_image_to_basis_ktx2() fixes beside the settings: UASTC LDR 4x4,
    // premultiplied alpha, basisu's default mip filter.
    builder.add_text_setting("format",            "uastc_ldr_4x4");
    builder.add_text_setting("alpha",             "premultiplied");
    builder.add_text_setting("mip_filter",        "kaiser");
    builder.add_uint_setting("uastc_level",       inputs.settings.uastc_level);
    builder.add_int_setting ("zstd_level",        inputs.settings.zstd_level);
    return builder.finish();
}

auto make_basis_texture_stamp(const std::string_view encoder_version) -> std::string
{
    return "ktx2;basis=uastc_ldr_4x4;basisu=" + std::string{encoder_version};
}

auto make_vertex_layout_signature(const erhe::gltf::Gltf_gpu_vertex_formats* const formats) -> std::string
{
    if (formats == nullptr) {
        return "none";
    }
    const auto signature = [](const erhe::dataformat::Vertex_format* format) -> std::string {
        return (format != nullptr) ? erhe::dataformat::get_vertex_format_signature(*format) : std::string{"-"};
    };
    return
        "base=" + signature(formats->base) +
        ";base_skinned=" + signature(formats->base_skinned) +
        ";optimized=" + signature(formats->optimized) +
        ";optimized_skinned=" + signature(formats->optimized_skinned) +
        ";index=" + std::string{erhe::dataformat::c_str(formats->index_type)} +
        ";encoding=" + std::to_string(formats->encoding_version);
}

auto c_str(const Scene_item_textures textures) -> const char*
{
    switch (textures) {
        case Scene_item_textures::source:   return "source";
        case Scene_item_textures::astc_4x4: return "astc_4x4";
        case Scene_item_textures::bc:       return "bc";
        default:                            return "?";
    }
}

auto make_scene_item_stamp(const Scene_item_textures textures) -> std::string
{
    return std::string{"glb;textures="} + c_str(textures);
}

auto make_scene_item_key(const std::string_view target, const std::string_view stamp, const std::span<const std::byte> scene_item) -> erhe::asset_store::Key
{
    erhe::asset_store::Key_builder builder{erhe::asset_store::Item_kind::scene};
    builder.add_code_version(c_scene_item_code_version);
    builder.add_format_stamp(stamp);
    builder.add_text_setting("target", target);
    builder.add_content_hash(erhe::asset_store::Content_hasher::hash(scene_item));
    return builder.finish();
}

auto make_pvs_stamp() -> std::string
{
    return "erhe_pvs=" + std::to_string(erhe::scene::Pvs_header::current_version);
}

auto make_pvs_key(const std::string_view stamp, const std::string_view meta, const std::span<const std::byte> payload) -> erhe::asset_store::Key
{
    erhe::asset_store::Key_builder builder{erhe::asset_store::Item_kind::pvs};
    builder.add_code_version(c_pvs_code_version);
    builder.add_format_stamp(stamp);
    // The bake's wall-clock time is no key input: an equal bake finds its item.
    nlohmann::json meta_json = nlohmann::json::parse(meta, nullptr, false);
    if (meta_json.is_object() && meta_json.contains("extension") && meta_json["extension"].contains("bake")) {
        meta_json["extension"]["bake"].erase("duration_seconds");
    }
    const std::string key_meta = meta_json.is_discarded() ? std::string{meta} : meta_json.dump();
    builder.add_content_hash(erhe::asset_store::Content_hasher::hash(std::as_bytes(std::span<const char>{key_meta.data(), key_meta.size()})));
    builder.add_content_hash(erhe::asset_store::Content_hasher::hash(payload));
    return builder.finish();
}

auto collect_scene_item_keys(const std::span<const std::byte> scene_item) -> std::optional<std::vector<erhe::asset_store::Key>>
{
    const std::optional<std::string_view> json_text = get_gltf_json_text(scene_item);
    if (!json_text.has_value()) {
        return std::nullopt;
    }
    // simdjson: the Scene item JSON is many MB, read before every prepared run.
    simdjson::dom::parser  parser;
    simdjson::dom::element document;
    if (parser.parse(json_text->data(), json_text->size()).get(document) != simdjson::SUCCESS) {
        return std::nullopt;
    }
    simdjson::dom::object root;
    if (document.get_object().get(root) != simdjson::SUCCESS) {
        return std::nullopt;
    }
    std::vector<erhe::asset_store::Key> keys;
    simdjson::dom::array buffers;
    if (root.at_key("buffers").get_array().get(buffers) == simdjson::SUCCESS) {
        for (const simdjson::dom::element buffer : buffers) {
            std::string_view uri;
            if (buffer.at_key("uri").get_string().get(uri) != simdjson::SUCCESS) {
                continue;
            }
            const std::optional<erhe::asset_store::Key> key = parse_item_uri(uri);
            if (key.has_value()) {
                keys.push_back(key.value());
            }
        }
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

}
