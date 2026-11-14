#pragma once

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>

// Scene-free parts of the spatial queries (doc/editor/level_tools.md "Spatial queries").

namespace editor {

enum class Level_region_shape : unsigned int {
    box = 0,
    sphere
};

// A region in the scene root's space (R10.1).
class Level_region
{
public:
    Level_region_shape shape {Level_region_shape::box};
    glm::vec3          min   {0.0f}; // box
    glm::vec3          max   {0.0f}; // box
    glm::vec3          center{0.0f}; // sphere
    float              radius{0.0f}; // sphere
};

// Touching counts as intersecting; an invalid bounds intersects nothing.
[[nodiscard]] auto region_intersects(const Level_region& region, const erhe::math::Aabb& bounds) -> bool;

inline constexpr int32_t     c_no_island              = -1;
inline constexpr std::size_t c_max_height_map_cells   = 65536;

class Height_map_cell
{
public:
    float   height  {0.0f}; // y of the top surface; meaningful when hit
    bool    hit     {false};
    bool    walkable{false}; // the hit lies on a navigation polygon
    int32_t island  {c_no_island};
};

// Cells of cell_size over the x and z extents of min .. max; rays run from max.y down to min.y.
class Height_map_grid
{
public:
    glm::vec3   min      {0.0f};
    glm::vec3   max      {0.0f};
    float       cell_size{1.0f};
    std::size_t columns  {0}; // along +x
    std::size_t rows     {0}; // along +z
};

// Zero columns and rows when the extents or the cell size are not positive; a partial last cell counts.
[[nodiscard]] auto make_height_map_grid(const glm::vec3& min, const glm::vec3& max, float cell_size) -> Height_map_grid;

// The x and z of the cell's center.
[[nodiscard]] auto get_cell_center(const Height_map_grid& grid, std::size_t column, std::size_t row) -> glm::vec2;

class Height_range
{
public:
    float min  {0.0f};
    float max  {0.0f};
    bool  valid{false}; // some cell has a hit
};

[[nodiscard]] auto get_height_range(std::span<const Height_map_cell> cells) -> Height_range;

// The digit 0 - 9 of a height over the range, linear and rounded; 0 when the range is empty.
[[nodiscard]] auto height_digit(float height, const Height_range& range) -> char;

// One line per row, the row of the smallest z first; a digit per hit cell, '.' per cell without a hit.
void write_height_text(const Height_map_grid& grid, std::span<const Height_map_cell> cells, std::string& out_text);

// As write_height_text: island id 0 - 9 then a - z (modulo 36) per walkable cell, '#' per hit not walkable, '.' per cell without a hit.
void write_island_text(const Height_map_grid& grid, std::span<const Height_map_cell> cells, std::string& out_text);

// D7.5 first pass: the index pair of two bounds that may interpenetrate (a < b).
class Bounds_pair
{
public:
    std::size_t a{0};
    std::size_t b{0};
};

// The smallest overlap of the two bounds over the three axes, negative when they are apart; no
// penetration depth exceeds it. Invalid bounds give a negative value.
[[nodiscard]] auto get_bounds_overlap(const erhe::math::Aabb& lhs, const erhe::math::Aabb& rhs) -> float;

// The center of the box both bounds cover.
[[nodiscard]] auto get_bounds_overlap_center(const erhe::math::Aabb& lhs, const erhe::math::Aabb& rhs) -> glm::vec3;

// The pairs whose bounds overlap by more than tolerance on every axis, sorted by a then b (a sweep
// along x). Cleared and filled; scratch_order is the caller's scratch.
void find_bounds_pairs(
    std::span<const erhe::math::Aabb> bounds,
    float                             tolerance,
    std::vector<std::size_t>&         scratch_order,
    std::vector<Bounds_pair>&         out_pairs
);

[[nodiscard]] auto has_any_tag(const std::set<std::string>& tags, std::span<const std::string> listed) -> bool;

// D7.6
enum class Level_finding_kind : unsigned int {
    unsupported = 0,       // R10.4
    overlap,               // R10.3
    unreachable_island,    // R9.10
    unwalkable_parameters, // R2.10
    unreadable_piece       // the piece record names no known shape or its parameters do not parse
};

[[nodiscard]] auto c_str(Level_finding_kind kind) -> const char*;

} // namespace editor
