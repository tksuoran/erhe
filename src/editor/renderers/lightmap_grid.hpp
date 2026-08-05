#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstdint>

namespace editor {

// Quadtree grid tile addressing for the lightmap world-space partition
// (doc/lightmap_baking_plan.md): the world XZ plane is covered by a uniform
// grid of user-sized cells (lightmap.cell_size_m, quadtree level 0),
// anchored at multiples of the cell size from the world origin - tile
// boundaries never move when content does. A cell at level L has side
// cell_size_m * 2^-L: subdividing (level + 1) halves the cell and doubles
// its nominal texel density (tile_texture_size / cell side), merging
// (level - 1) does the opposite.
//
// A key {level, ix, iz} covers [ix*s, (ix+1)*s) x [iz*s, (iz+1)*s) with
// s = cell size at that level. Keys round-trip through glm::ivec3
// (Scene_settings::lightmap_tile_overrides stores them that way).
class Lightmap_tile_key
{
public:
    int level{0};
    int ix   {0};
    int iz   {0};

    Lightmap_tile_key() = default;
    Lightmap_tile_key(const int level, const int ix, const int iz) : level{level}, ix{ix}, iz{iz} {}
    explicit Lightmap_tile_key(const glm::ivec3& v) : level{v.x}, ix{v.y}, iz{v.z} {}
    [[nodiscard]] auto to_ivec3() const -> glm::ivec3 { return glm::ivec3{level, ix, iz}; }

    [[nodiscard]] auto operator==(const Lightmap_tile_key& other) const -> bool = default;

    // Strict ordering for deterministic tile emission.
    [[nodiscard]] auto operator<(const Lightmap_tile_key& other) const -> bool
    {
        if (level != other.level) return level < other.level;
        if (iz    != other.iz   ) return iz    < other.iz;
        return ix < other.ix;
    }

    [[nodiscard]] static auto cell_size(const float base_cell_size, const int level) -> float
    {
        return base_cell_size * std::exp2(static_cast<float>(-level));
    }
    [[nodiscard]] auto cell_size(const float base_cell_size) const -> float
    {
        return cell_size(base_cell_size, level);
    }
    // World XZ extents of the cell.
    [[nodiscard]] auto min_corner(const float base_cell_size) const -> glm::vec2
    {
        const float s = cell_size(base_cell_size);
        return glm::vec2{static_cast<float>(ix) * s, static_cast<float>(iz) * s};
    }
    [[nodiscard]] auto max_corner(const float base_cell_size) const -> glm::vec2
    {
        const float s = cell_size(base_cell_size);
        return glm::vec2{static_cast<float>(ix + 1) * s, static_cast<float>(iz + 1) * s};
    }
    [[nodiscard]] static auto for_position(const float base_cell_size, const int level, const float x, const float z) -> Lightmap_tile_key
    {
        const float s = cell_size(base_cell_size, level);
        return Lightmap_tile_key{
            level,
            static_cast<int>(std::floor(x / s)),
            static_cast<int>(std::floor(z / s))
        };
    }
    [[nodiscard]] auto parent() const -> Lightmap_tile_key
    {
        // floor division so negative indices parent correctly
        const auto floor_div_2 = [](const int v) -> int { return (v >= 0) ? (v / 2) : ((v - 1) / 2); };
        return Lightmap_tile_key{level - 1, floor_div_2(ix), floor_div_2(iz)};
    }
    [[nodiscard]] auto children() const -> std::array<Lightmap_tile_key, 4>
    {
        return {
            Lightmap_tile_key{level + 1, 2 * ix,     2 * iz    },
            Lightmap_tile_key{level + 1, 2 * ix + 1, 2 * iz    },
            Lightmap_tile_key{level + 1, 2 * ix,     2 * iz + 1},
            Lightmap_tile_key{level + 1, 2 * ix + 1, 2 * iz + 1}
        };
    }
    // Does this key's area contain the given key's area (or equal it)?
    // Only meaningful when this->level <= key.level.
    [[nodiscard]] auto contains(const Lightmap_tile_key& key) const -> bool
    {
        if (key.level < level) {
            return false;
        }
        const int shift = key.level - level;
        const auto floor_shift = [shift](const int v) -> int {
            // arithmetic shift of possibly negative index = floor division
            return v >> shift;
        };
        return (floor_shift(key.ix) == ix) && (floor_shift(key.iz) == iz);
    }
};

class Lightmap_tile_key_hash
{
public:
    [[nodiscard]] auto operator()(const Lightmap_tile_key& key) const -> std::size_t
    {
        std::size_t h = static_cast<std::size_t>(static_cast<uint32_t>(key.level));
        h = h * 0x9E3779B97F4A7C15ull + static_cast<std::size_t>(static_cast<uint32_t>(key.ix));
        h = h * 0x9E3779B97F4A7C15ull + static_cast<std::size_t>(static_cast<uint32_t>(key.iz));
        return h;
    }
};

}
