#pragma once

#include "level/level_repeat.hpp"
#include "level/stairs_mesh.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::math { class Aabb; class Curve_path; class Curve_path_table; }

// Scene-free values of a kit layout node (doc/editor/level_tools.md "Kit layout nodes", D3.9).

namespace editor {

enum class Kit_snap : unsigned int {
    none = 0,
    module // positions round to multiples of the module (R5.2)
};

// How a line, ring or path gets its piece count (R5.12.7); a grid always states its counts.
enum class Kit_layout_count_mode : unsigned int {
    count = 0, // the count is stated
    gap        // the free distance between neighbors is stated, the space gives the count
};

// How a scatter gets its piece count: stated, or the density times the region's area.
enum class Kit_scatter_mode : unsigned int {
    count = 0,
    density
};

// The range of a scatter region's size_x, size_z and region_radius, as the rows and the region handles clamp them (meters).
inline constexpr float c_min_scatter_region_size{0.01f};
inline constexpr float c_max_scatter_region_size{10000.0f};

// The ranges of the line, grid and ring values the rows and the layout handles keep (meters; the arc in degrees).
inline constexpr float c_max_line_step   {1000.0f};
inline constexpr float c_max_line_length {10000.0f};
inline constexpr float c_max_grid_spacing{1000.0f};
inline constexpr float c_min_ring_radius {0.01f};
inline constexpr float c_max_ring_radius {1000.0f};
inline constexpr float c_min_ring_arc    {1.0f};
inline constexpr float c_max_ring_arc    {360.0f};
// A line step or grid spacing a layout handle sets keeps its sign and at least this size.
inline constexpr float c_min_layout_handle_step{0.01f};

[[nodiscard]] auto c_str(Kit_layout_count_mode mode) -> const char*;
[[nodiscard]] auto c_str(Kit_scatter_mode mode) -> const char*;
[[nodiscard]] auto c_str(Repeat_ring_facing facing) -> const char*;

// The arrangement in the layout node's own frame, element 0 at its origin; every kind's values are kept.
class Kit_layout_pattern
{
public:
    Repeat_pattern_kind    kind       {Repeat_pattern_kind::line};
    Kit_layout_count_mode  count_mode {Kit_layout_count_mode::count};
    int                    count      {3};                  // line, ring, path, scatter in count mode
    float                  gap        {0.0f};               // line, ring, path in gap mode
    glm::vec3              direction  {1.0f, 0.0f, 0.0f};   // line
    float                  step       {1.0f};               // line in count mode: center to center
    float                  length     {4.0f};               // line in gap mode: from the first center to the last
    int                    count_x    {3};                  // grid
    int                    count_z    {3};                  // grid
    float                  spacing_x  {1.0f};               // grid
    float                  spacing_z  {1.0f};               // grid
    float                  radius     {4.0f};               // ring
    float                  arc_degrees{360.0f};             // ring: 360 is a full ring
    Repeat_ring_facing     facing     {Repeat_ring_facing::outward};
    Repeat_path_orientation orientation{Repeat_path_orientation::upright}; // path, segments; the curve is the layout node itself
    Kit_scatter_mode       scatter_mode {Kit_scatter_mode::count};          // scatter, centred on the node's origin
    float                  density      {0.25f};                            // scatter in density mode: pieces per square meter
    Repeat_scatter_region  region       {Repeat_scatter_region::rectangle}; // scatter
    float                  size_x       {8.0f};                             // scatter rectangle
    float                  size_z       {8.0f};                             // scatter rectangle
    float                  region_radius{4.0f};                             // scatter disc
    float                  min_spacing  {1.0f};                             // scatter: between piece footprints
    float                  yaw_min      {0.0f};                             // scatter: degrees about the node's up axis
    float                  yaw_max      {360.0f};                           // scatter
    Stairs_values          stairs;                                          // stairs, along the node's own curve
    std::string            stairs_material;                                 // stairs: a scene or palette material; empty: the stone slot's default

    [[nodiscard]] auto operator==(const Kit_layout_pattern& other) const -> bool = default;
};

// One kit item a layout places: a shape, its parameter values, a material name per slot (empty: the default) and the
// weight a scatter picks it by.
class Kit_layout_item
{
public:
    std::string              shape;
    std::string              parameters; // Kit_parameter_values canonical text, its derive mask the item's seed mask
    std::vector<std::string> materials;
    uint32_t                 weight{1};  // 0 - c_max_scatter_weight; the other kinds take item i modulo the item count

    [[nodiscard]] auto operator==(const Kit_layout_item& other) const -> bool = default;
};

// The line step and the grid spacings of a contiguous run of an item with this bounds extent (x for the step).
void set_layout_spacing_from_extent(Kit_layout_pattern& pattern, const glm::vec3& extent);

// Every layout kind, in the order the Create menu lists them (U47).
inline constexpr Repeat_pattern_kind c_layout_kinds[] = {
    Repeat_pattern_kind::line, Repeat_pattern_kind::grid, Repeat_pattern_kind::ring, Repeat_pattern_kind::scatter,
    Repeat_pattern_kind::path, Repeat_pattern_kind::segments, Repeat_pattern_kind::stairs
};

// U47: the Create menu's entry and the new node's name for a layout of the kind ("Line Layout", ..., "Path Stairs").
[[nodiscard]] auto get_layout_kind_create_label(Repeat_pattern_kind kind) -> const char*;

// Wrap with Path (U45): the fewest elements its straight curve holds.
inline constexpr std::size_t c_wrap_path_min_count{4};

// U45: a path in gap mode with no gap, so the wrapped items stand as a contiguous run that grows with the curve.
[[nodiscard]] auto make_wrap_path_pattern() -> Kit_layout_pattern;

// U45: the length of the straight curve: c_wrap_path_min_count elements of the extent, or one per item when there are
// more; 1 m when the extent is not positive.
[[nodiscard]] auto get_wrap_path_length(float item_extent, std::size_t item_count) -> float;

// Appends the item unless an equal one is listed; returns true when appended.
auto append_distinct_layout_item(std::vector<Kit_layout_item>& items, const Kit_layout_item& item) -> bool;

// JSON objects with sorted keys and hexadecimal float strings; equal values give equal text.
[[nodiscard]] auto to_canonical_text(const Kit_layout_pattern& pattern) -> std::string;
[[nodiscard]] auto to_canonical_text(std::span<const Kit_layout_item> items) -> std::string;

// Return the problem, empty when read; out is written only on success.
[[nodiscard]] auto parse_layout_pattern(std::string_view text, Kit_layout_pattern& out_pattern) -> std::string;
[[nodiscard]] auto parse_layout_items  (std::string_view text, std::vector<Kit_layout_item>& out_items) -> std::string;

// What a pattern reads of the items: the largest extent along x (gap mode), and per item its weight, footprint and x bounds.
class Kit_layout_item_sizes
{
public:
    float                            extent{0.0f};
    std::vector<Repeat_scatter_item> items;
    std::vector<Repeat_segment_item> segment_items; // per item its x bounds (a segments run)

    void clear();
    // bounds: the item's brush bounds; invalid bounds (an item that does not read) count as a point.
    void add(const Kit_layout_item& item, const erhe::math::Aabb& bounds);
};

// The radius of the circle about the origin's vertical axis around the bounds' xz rectangle; 0 for invalid bounds.
[[nodiscard]] auto get_scatter_footprint_radius(const erhe::math::Aabb& bounds) -> float;

// The run a pattern makes: a count mode Repeat_pattern for expand_repeat() from the node's origin (a path: the node is
// its curve, so its own frame).
class Kit_layout_run
{
public:
    Repeat_pattern pattern;
    std::size_t    count{0};
    float          step {0.0f}; // center to center along the space (the arc for a ring); 0 for one element and a grid
    float          space{0.0f}; // line: first to last center; ring: arc length; path: curve length; grid: 0; scatter: area;
                                // segments: the spanned chord lengths' sum; stairs: the curve's plan length
    std::size_t    requested{0}; // the pieces the values ask for; a scatter holds fewer when no more fit the spacing
};

// The material slot of a stairs layout's staircase, the one of basic.stairs.
inline constexpr const char* c_stairs_material_slot = "stone";

// seed: the layout seed, a scatter's point stream; items: one entry per layout item (the run refers to them);
// path_table: a path's, a segments run's or the stairs' curve (the run refers to it), null for the other kinds; out_poses: when given, the run's poses
// from the node's origin with each pose's item, cleared and filled. Returns the refusal, empty when resolved.
[[nodiscard]] auto resolve_layout_pattern(
    const Kit_layout_pattern&           pattern,
    int                                 seed,
    const Kit_layout_item_sizes&        items,
    const erhe::math::Curve_path_table* path_table,
    Kit_layout_run&                     out_run,
    std::vector<Repeat_pose>*           out_poses
) -> std::string;

// The path with every position and handle end taken through the transform; cleared and filled.
void transform_curve_path(const erhe::math::Curve_path& path, const glm::mat4& transform, erhe::math::Curve_path& out_path);

// What a stairs layout's staircase is generated from, as text: the curve and the stairs values, hexadecimal floats, so
// equal inputs give equal text.
[[nodiscard]] auto make_stairs_source_text(const erhe::math::Curve_path& path, const Stairs_values& values) -> std::string;

// Reads a source text of the current generator version; returns the problem, empty when read.
[[nodiscard]] auto parse_stairs_source_text(std::string_view text, erhe::math::Curve_path& out_path, Stairs_values& out_values) -> std::string;

[[nodiscard]] auto get_layout_item_index(std::size_t element, std::size_t item_count) -> std::size_t;

// The seed of element i with per-element seeds: the same on every platform, a function of the layout seed and i alone.
[[nodiscard]] auto derive_layout_element_seed(int layout_seed, std::size_t element) -> int;

} // namespace editor
