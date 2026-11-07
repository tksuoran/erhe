#pragma once

#include "erhe_asset_store/key.hpp"
#include "erhe_graphics/basis_encoder.hpp"
#include "erhe_hash/content_hash.hpp"
#include "erhe_graphics/ktx2_container.hpp"
#include "erhe_graphics/texture_properties.hpp"
#include "erhe_primitive/lod.hpp"
#include "erhe_primitive/mesh_optimizer.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::gltf      { class Gltf_gpu_vertex_formats; }
namespace erhe::primitive { class Triangle_soup; }

// The keys of the items an Export makes (doc/plans/prepared_assets_design.md
// D3) and the uri a Scene item names an item by (D1); doc/editor/prepared_assets.md.
namespace editor {

inline constexpr std::string_view c_item_uri_prefix           = "erhe-item:";
inline constexpr std::uint32_t    c_local_mesh_code_version   = 2;
inline constexpr std::uint32_t    c_raytrace_bvh_code_version = 1;
inline constexpr std::uint32_t    c_scene_item_code_version   = 2;
inline constexpr std::uint32_t    c_texture_code_version      = 1;
inline constexpr std::uint32_t    c_basis_texture_code_version = 1;
inline constexpr std::uint32_t    c_world_mesh_code_version   = 1;
inline constexpr std::uint32_t    c_pvs_code_version          = 1;
// Editor asset record: the key of the scene's PVS item, the last bake (R4.10).
inline constexpr std::string_view c_pvs_key_record            = "pvs.key";

// "erhe-item:<key as 32 hex digits>"
[[nodiscard]] auto make_item_uri (const erhe::asset_store::Key& key) -> std::string;
// The key of an item uri; nullopt for any other string.
[[nodiscard]] auto parse_item_uri(std::string_view uri) -> std::optional<erhe::asset_store::Key>;

// A content hash as the key-field value it is (the same 16 bytes).
[[nodiscard]] auto to_key(const erhe::hash::Content_hash& content_hash) -> erhe::asset_store::Key;

// erhe::primitive::compute_triangle_soup_content_hash() as a key-field value:
// XXH3-128 over the soup's primitive type, its indices and, per attribute in
// layout order, the attribute descriptor and every vertex's attribute bytes.
[[nodiscard]] auto compute_soup_content_hash(const erhe::primitive::Triangle_soup& soup) -> erhe::asset_store::Key;

// What the content hash of a Local-space mesh item covers: the primitive's
// triangle soup, or the triangles derived from its Geometry
// (erhe::primitive::derive_triangle_soup()), which prepare differently.
enum class Local_mesh_source : unsigned int
{
    soup = 0,
    derived_triangles
};

// What a Local-space mesh item depends on beside its code version.
class Local_mesh_key_inputs
{
public:
    Local_mesh_source                            source{Local_mesh_source::soup};
    erhe::asset_store::Key                       content_hash;
    std::optional<erhe::primitive::Lod_settings> lod_settings;
    erhe::primitive::Mesh_optimize_options       optimize_options;
    std::string                                  stamp;
};

[[nodiscard]] auto make_local_mesh_key(const Local_mesh_key_inputs& inputs) -> erhe::asset_store::Key;

// The Local-space mesh key a primitive shape asks for: its content hash, its
// source and its LOD request, with the mesh optimizer options Export and the
// editor's own loading prepare with (the one key computation of both).
[[nodiscard]] auto make_shape_local_mesh_key(
    Local_mesh_source                                   source,
    const erhe::hash::Content_hash&                     content_hash,
    const std::optional<erhe::primitive::Lod_settings>& lod_settings,
    std::string_view                                    stamp
) -> erhe::asset_store::Key;

// What a World-space mesh item depends on (R4.7): its Local-space mesh item's
// key, the node's world transform relative to the scene root as exact bits,
// and the stamp it shares with mesh items.
class World_mesh_key_inputs
{
public:
    erhe::asset_store::Key local_mesh_key;
    glm::mat4              world_from_node{1.0f};
    std::string            stamp;
};

[[nodiscard]] auto make_world_mesh_key(const World_mesh_key_inputs& inputs) -> erhe::asset_store::Key;

// The 16 floats of a transform, column-major, as hexfloat text ("%a"), one space apart.
[[nodiscard]] auto format_transform_hexfloat(const glm::mat4& transform) -> std::string;

// Format stamp of a Local-space mesh item: the vertex layout signature of its
// stored GPU streams (any text that names the layouts), "layout=<XXH3-128>".
[[nodiscard]] auto make_local_mesh_stamp(std::string_view vertex_layout_signature) -> std::string;

// Format stamp of a Raytrace BVH item: "bvh=<backend>/<BVH library tag>".
[[nodiscard]] auto make_raytrace_bvh_stamp(std::string_view bvh_backend, std::string_view bvh_library) -> std::string;

// The key of the Raytrace BVH item of a mesh item: its code version, its
// format stamp and the mesh item's key (R4.1).
[[nodiscard]] auto make_raytrace_bvh_key(const erhe::asset_store::Key& mesh_key, std::string_view stamp) -> erhe::asset_store::Key;

// What a Texture item is made from (R7.5): a Basis Universal source
// transcoded, a PNG / JPEG image transcoded from its Basis texture item
// (texture_encoding target), or a PNG / JPEG image's bytes as they are
// (texture_encoding source).
enum class Texture_item_source : unsigned int
{
    basis_source = 0,
    basis_texture_item,
    source_bytes
};

// What a Texture item depends on beside its code version (R4.1, R3.2): the
// source image's content hash (basis_texture_item: the Basis texture item's
// key), and for a transcode the texture properties it reads and the export
// setting of its supercompression; the format is in the stamp.
class Texture_key_inputs
{
public:
    Texture_item_source                         source{Texture_item_source::basis_source};
    erhe::asset_store::Key                      content_hash; // XXH3-128 of the source image bytes, or the Basis texture item's key
    erhe::graphics::Texture_transfer_function   transfer_function{erhe::graphics::Texture_transfer_function::srgb};
    erhe::graphics::Texture_normal_encoding     normal_encoding  {erhe::graphics::Texture_normal_encoding::none};
    erhe::graphics::Ktx2_supercompression       supercompression {erhe::graphics::Ktx2_supercompression::zstd};
    std::string                                 stamp;
};

[[nodiscard]] auto make_texture_key(const Texture_key_inputs& inputs) -> erhe::asset_store::Key;

// Format stamp of a Texture item: "ktx2;blocks=<astc_4x4 | bc>", the block
// format a build must sample or decode.
[[nodiscard]] auto make_texture_stamp(erhe::graphics::Ktx2_block_target block_target) -> std::string;
// Format stamp of a Texture item holding a PNG / JPEG image as it is: "source"
// (every build decodes it; the item is shared by the targets, R3.10).
[[nodiscard]] auto make_source_texture_stamp() -> std::string;

// What a Basis texture item depends on beside its code version (R4.1): the
// source image's content hash, every texture property the encode reads and
// the encoder settings; the encoder version is in the stamp.
class Basis_texture_key_inputs
{
public:
    erhe::asset_store::Key                content_hash; // XXH3-128 of the source image bytes
    erhe::graphics::Texture_properties    properties;
    erhe::graphics::Basis_encode_settings settings;
    std::string                           stamp;
};

[[nodiscard]] auto make_basis_texture_key(const Basis_texture_key_inputs& inputs) -> erhe::asset_store::Key;

// Format stamp of a Basis texture item: "ktx2;basis=uastc_ldr_4x4;basisu=<encoder version>".
[[nodiscard]] auto make_basis_texture_stamp(std::string_view encoder_version) -> std::string;

// The text naming the GPU vertex formats (and index format, encoding version)
// of the stored streams; "none" without formats.
[[nodiscard]] auto make_vertex_layout_signature(const erhe::gltf::Gltf_gpu_vertex_formats* formats) -> std::string;

// How a Scene item holds its images: as their source bytes, or transcoded to
// a block format (the texture families of a prepared target).
enum class Scene_item_textures : unsigned int
{
    source = 0,
    astc_4x4,
    bc
};
[[nodiscard]] auto c_str(Scene_item_textures textures) -> const char*;
// Format stamp of a Scene item: "glb;textures=<source | astc_4x4 | bc>".
[[nodiscard]] auto make_scene_item_stamp(Scene_item_textures textures) -> std::string;

// The key of a Scene item: its code version, its format stamp, its target and
// the content hash of its bytes (a Scene item is made per target, R4.1).
[[nodiscard]] auto make_scene_item_key(std::string_view target, std::string_view stamp, std::span<const std::byte> scene_item) -> erhe::asset_store::Key;

// Format stamp of a PVS item: "erhe_pvs=<ERHE_pvs version>".
[[nodiscard]] auto make_pvs_stamp() -> std::string;

// The key of a PVS item: outside R3.2 (its inputs are not tracked, R4.10),
// it is the content hash of what the bake made - its meta (without the
// bake's duration) and payload.
[[nodiscard]] auto make_pvs_key(std::string_view stamp, std::string_view meta, std::span<const std::byte> payload) -> erhe::asset_store::Key;

// The keys a Scene item (GLB or glTF JSON) names through erhe-item: buffer
// uris, sorted and unique; nullopt when the document does not parse.
[[nodiscard]] auto collect_scene_item_keys(std::span<const std::byte> scene_item) -> std::optional<std::vector<erhe::asset_store::Key>>;

}
