#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace erhe::scene { class Node; }

namespace editor {

// On-disk persistence for baked lightmap tiles (one file per tile plus a
// JSON manifest), enabling bake-once / stream-at-runtime with bounded
// memory regardless of world extents:
//
//   <scene_path>.lightmap/
//     manifest.json     - version, tile size, density, bake-parameter hash,
//                         per tile: id, world bounds, payload file, and the
//                         region table (node path + mesh name + primitive
//                         index -> tile-local uv scale/offset).
//     tile_<id>.lmt     - 64-byte binary header (magic ELMT) + tightly
//                         packed RGBA16F rows (32 MiB at 2048^2).
//
// Region identity is node path (+ node index path for duplicate-name
// disambiguation) + mesh name + primitive index - the only reload-stable
// handles erhe has (Item ids are runtime-only). Renaming a node orphans its
// regions: they render unlit until rebaked, never corrupt.
//
// World-space tile pieces (Lightmap_partitioner) store the identity of the
// SOURCE mesh plus (tile, piece_ordinal): pieces are re-created
// deterministically by Prepare World-Space Tiles, so the streamer resolves
// them through the live partition rather than by scene-name lookup.
class Lightmap_tile_io
{
public:
    static constexpr uint32_t c_manifest_version = 3;
    static constexpr uint32_t c_payload_version  = 1;
    static constexpr uint32_t c_payload_magic    = 0x544D4C45u; // 'ELMT' little-endian

    enum class Payload_format : uint32_t {
        rgba16f = 0 // raw tightly packed fp16 RGBA rows
    };

    class Region_entry
    {
    public:
        std::string node_path;          // '/'-joined ancestor names
        std::string node_index_path;    // '/'-joined root-to-node child indices (unique even for duplicate names)
        std::string mesh_name;
        std::size_t primitive_index{0}; // pieces: the SOURCE mesh's primitive index
        int         piece_ordinal{-1};  // >= 0: world-space tile piece (see class comment)
        glm::vec4   uv_scale_offset{0.0f, 0.0f, 0.0f, 0.0f}; // chart UV -> tile UV
    };

    class Tile_entry
    {
    public:
        int                       id{0};    // index within THIS manifest (kd leaf tile index of the layout that wrote it)
        // Quadtree grid identity (manifest v3): {level, ix, iz} anchored at
        // the world origin - stable across sessions and content edits, the
        // key save/restore and the streamer match tiles by.
        int                       level{0};
        int                       ix{0};
        int                       iz{0};
        // Nominal texel density of the tile (tile_texture_size / cell
        // side); effective baked density = this * density_scale.
        float                     texels_per_meter{0.0f};
        glm::vec3                 bounds_min{0.0f};
        glm::vec3                 bounds_max{0.0f};
        float                     density_scale{1.0f};
        std::string               payload;  // file name inside the set directory
        std::vector<Region_entry> regions;
    };

    class Manifest
    {
    public:
        uint32_t                version         {c_manifest_version};
        int                     tile_size       {2048};
        float                   texels_per_meter{16.0f};
        uint64_t                bake_hash       {0};
        std::vector<Tile_entry> tiles;
    };

    [[nodiscard]] static auto directory_for_scene(const std::filesystem::path& scene_path) -> std::filesystem::path;
    // Grid-keyed payload file name: tile_L<level>_<ix>_<iz>.lmt (negative
    // indices keep their minus sign).
    [[nodiscard]] static auto payload_name       (int level, int ix, int iz) -> std::string;

    // '/'-joined ancestor names for the manifest's reload-stable region key.
    [[nodiscard]] static auto node_path(const erhe::scene::Node* node) -> std::string;
    // '/'-joined root-to-node child indices; unique per node even when
    // sibling names collide (the duplicate-name disambiguator).
    [[nodiscard]] static auto node_index_path(const erhe::scene::Node* node) -> std::string;

    // Manifest round-trip; on failure returns false and (when non-null)
    // fills error with the reason.
    static auto write_manifest(const std::filesystem::path& directory, const Manifest& manifest, std::string* error) -> bool;
    static auto read_manifest (const std::filesystem::path& directory, Manifest& out_manifest, std::string* error) -> bool;

    // Tile payload round-trip. rgba16 is width * height * 4 fp16 values
    // (bit patterns); write converts nothing - callers convert from float
    // with float_to_half() when needed. sweeps is the accumulation sweep
    // count behind the payload (0 = unknown, e.g. payloads written before
    // the field existed); restore-on-activate uses it as the republish
    // hold threshold.
    static auto write_tile_payload(
        const std::filesystem::path& file_path,
        int                          width,
        int                          height,
        std::span<const uint16_t>    rgba16,
        uint64_t                     bake_hash,
        const glm::vec3&             bounds_min,
        const glm::vec3&             bounds_max,
        uint32_t                     sweeps,
        std::string*                 error
    ) -> bool;
    static auto read_tile_payload(
        const std::filesystem::path& file_path,
        int&                         out_width,
        int&                         out_height,
        std::vector<uint16_t>&       out_rgba16,
        uint64_t*                    out_bake_hash,
        uint32_t*                    out_sweeps,
        std::string*                 error
    ) -> bool;

    [[nodiscard]] static auto float_to_half(float value) -> uint16_t;
};

}
