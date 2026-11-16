#pragma once

#include "level/kit_layout_values.hpp"
#include "level/level_repeat.hpp"

#include <glm/glm.hpp>

#include <span>
#include <vector>

namespace erhe::math { class Curve_path_table; }

// Scene-free shape a kit layout node draws (doc/editor/level_tools.md "Kit layout drawing").

namespace editor {

// A tick across the outline at position, along the unit direction; in the layout node's frame.
class Kit_layout_tick
{
public:
    glm::vec3 position {0.0f};
    glm::vec3 direction{1.0f, 0.0f, 0.0f};
};

// An arc about the node's y axis: the point at angle a is center + radius * (sin a, 0, cos a), a from 0 to degrees.
class Kit_layout_arc
{
public:
    glm::vec3 center {0.0f};
    float     radius {0.0f};
    float     degrees{360.0f};
};

// A scatter point with its item's footprint radius and that radius plus half the minimum spacing.
class Kit_layout_spacing_circle
{
public:
    glm::vec3 center          {0.0f};
    float     footprint_radius{0.0f};
    float     spacing_radius  {0.0f};
};

// The value a scatter region handle edits (doc/editor/level_tools.md "Scatter region handles").
enum class Kit_region_handle_value : unsigned int
{
    size_x = 0,   // a rectangle's edge across the x axis
    size_z,       // a rectangle's edge across the z axis
    size_x_z,     // a rectangle's corner: both sizes
    region_radius // a disc's handle
};

[[nodiscard]] auto c_str(Kit_region_handle_value value) -> const char*;

// A handle on a scatter's region outline in the node's frame; side holds the sign of each axis it edits (0 elsewhere).
class Kit_region_handle
{
public:
    glm::vec3               position{0.0f};
    glm::vec3               side    {1.0f, 0.0f, 0.0f};
    Kit_region_handle_value value   {Kit_region_handle_value::size_x};
};

// What a kit layout node draws, in its own frame; a path draws its curve instead, so its outline is empty; segments draw
// the chords through the curve's points with a tick at each point.
class Kit_layout_outline
{
public:
    Repeat_pattern_kind                    kind  {Repeat_pattern_kind::line};
    std::vector<glm::vec3>                 polyline;
    bool                                   closed{false};
    std::vector<Kit_layout_arc>            arcs;
    std::vector<Kit_layout_tick>           ticks;
    std::vector<Kit_layout_spacing_circle> circles;
    std::vector<Kit_region_handle>         region_handles; // a scatter's: a rectangle's 4 corners then 4 edges, a disc's 4

    void clear();
};

// From the pattern, its resolved run and the run's poses (a scatter's with each pose's item, from the node's origin);
// items: the footprints the run was resolved with; path_table: a segments layout's curve, else null. Cleared and filled.
void make_layout_outline(
    const Kit_layout_pattern&                 pattern,
    const Kit_layout_run&                     run,
    std::span<const Repeat_pose>              poses,
    std::span<const Repeat_scatter_item>      items,
    const erhe::math::Curve_path_table*       path_table,
    Kit_layout_outline&                       out_outline
);

// The region values of the handle moved to point (the node's frame; y is ignored), symmetric about the node's origin
// and clamped to c_min_scatter_region_size - c_max_scatter_region_size; the other values are kept.
void apply_region_handle(const Kit_region_handle& handle, const glm::vec3& point, Kit_layout_pattern& in_out_pattern);

[[nodiscard]] auto get_arc_point(const Kit_layout_arc& arc, float degrees) -> glm::vec3;

} // namespace editor
