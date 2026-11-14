#pragma once

#include "level/level_repeat.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Scene-free values of a kit layout node (doc/editor/level_tools.md "Kit layout nodes", D3.9).

namespace editor {

enum class Kit_snap : unsigned int {
    none = 0,
    module // positions round to multiples of the module (R5.2)
};

// How a line, ring or polyline gets its piece count (R5.12.7); a grid always states its counts.
enum class Kit_layout_count_mode : unsigned int {
    count = 0, // the count is stated
    gap        // the free distance between neighbors is stated, the space gives the count
};

[[nodiscard]] auto c_str(Kit_layout_count_mode mode) -> const char*;
[[nodiscard]] auto c_str(Repeat_ring_facing facing) -> const char*;

// The arrangement in the layout node's own frame, element 0 at its origin; every kind's values are kept.
class Kit_layout_pattern
{
public:
    Repeat_pattern_kind    kind       {Repeat_pattern_kind::line};
    Kit_layout_count_mode  count_mode {Kit_layout_count_mode::count};
    int                    count      {3};                  // line, ring, polyline in count mode
    float                  gap        {0.0f};               // line, ring, polyline in gap mode
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
    std::vector<glm::vec3> points;                          // polyline: after the origin

    [[nodiscard]] auto operator==(const Kit_layout_pattern& other) const -> bool = default;
};

// One kit item a layout places: a shape, its parameter values and a material name per slot (empty: the default).
class Kit_layout_item
{
public:
    std::string              shape;
    std::string              parameters; // Kit_parameter_values canonical text, its derive mask the item's seed mask
    std::vector<std::string> materials;

    [[nodiscard]] auto operator==(const Kit_layout_item& other) const -> bool = default;
};

// The line step and the grid spacings of a contiguous run of an item with this bounds extent (x for the step).
void set_layout_spacing_from_extent(Kit_layout_pattern& pattern, const glm::vec3& extent);

// JSON objects with sorted keys and hexadecimal float strings; equal values give equal text.
[[nodiscard]] auto to_canonical_text(const Kit_layout_pattern& pattern) -> std::string;
[[nodiscard]] auto to_canonical_text(std::span<const Kit_layout_item> items) -> std::string;

// Return the problem, empty when read; out is written only on success.
[[nodiscard]] auto parse_layout_pattern(std::string_view text, Kit_layout_pattern& out_pattern) -> std::string;
[[nodiscard]] auto parse_layout_items  (std::string_view text, std::vector<Kit_layout_item>& out_items) -> std::string;

// The run a pattern makes: a count mode Repeat_pattern for expand_repeat() from the node's origin.
class Kit_layout_run
{
public:
    Repeat_pattern pattern;
    std::size_t    count{0};
    float          step {0.0f}; // center to center along the space (the arc for a ring); 0 for one element and a grid
    float          space{0.0f}; // line: first to last center; ring: arc length; polyline: path length; grid: 0
};

// item_extent: the largest extent along x of the items placed, read in gap mode. Returns the refusal, empty when resolved.
[[nodiscard]] auto resolve_layout_pattern(const Kit_layout_pattern& pattern, float item_extent, Kit_layout_run& out_run) -> std::string;

// The length of the path from the origin through the points.
[[nodiscard]] auto get_path_length(std::span<const glm::vec3> points) -> double;

[[nodiscard]] auto get_layout_item_index(std::size_t element, std::size_t item_count) -> std::size_t;

// The seed of element i with per-element seeds: the same on every platform, a function of the layout seed and i alone.
[[nodiscard]] auto derive_layout_element_seed(int layout_seed, std::size_t element) -> int;

} // namespace editor
