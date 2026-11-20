#pragma once

#include "level/kit_layout_values.hpp"
#include "level/level_repeat.hpp"

#include <glm/glm.hpp>

#include <cstddef>
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

// The value a layout handle edits (doc/editor/level_tools.md "Layout handles").
enum class Kit_region_handle_value : unsigned int
{
    size_x = 0,    // a scatter rectangle's edge across the x axis
    size_z,        // a scatter rectangle's edge across the z axis
    size_x_z,      // a scatter rectangle's corner: both sizes
    region_radius, // a scatter disc's handle
    step,          // a line in count mode: its run's end
    length,        // a line in gap mode: the end of its length
    spacing_x,     // a grid's far edge across the x axis
    spacing_z,     // a grid's far edge across the z axis
    spacing_x_z,   // a grid's far corner: both spacings
    radius,        // a ring's handle at the arc's middle
    arc_degrees    // a ring's handle at the arc's end, inside the ring
};

[[nodiscard]] auto c_str(Kit_region_handle_value value) -> const char*;

// A layout handle in the node's frame; what side holds per value: doc/editor/level_tools.md "Layout handles".
class Kit_region_handle
{
public:
    glm::vec3               position{0.0f};
    glm::vec3               side    {1.0f, 0.0f, 0.0f};
    Kit_region_handle_value value   {Kit_region_handle_value::size_x};
};

// A ring's arc handle sits this fraction of the radius from the center, so a full ring's stays clear of element 0.
inline constexpr float c_ring_arc_handle_inset{0.75f};

// A ring's radius handle sits at least this angle around the circle from element 0: at least one radius away from it.
inline constexpr float c_ring_radius_handle_min_degrees{60.0f};

// What a handle drag carries from step to step: the arc's angle before the clamp, so a held end stays held.
class Kit_layout_handle_drag
{
public:
    float arc_degrees{0.0f};
};

[[nodiscard]] auto begin_layout_handle_drag(const Kit_layout_pattern& start) -> Kit_layout_handle_drag;

// What the handle indices of a layout's values name; when it changes they name other handles.
class Kit_layout_handle_set
{
public:
    Repeat_pattern_kind   kind      {Repeat_pattern_kind::line};
    Kit_layout_count_mode count_mode{Kit_layout_count_mode::count}; // a line's
    Repeat_scatter_region region    {Repeat_scatter_region::rectangle}; // a scatter's
    std::size_t           count     {0};

    [[nodiscard]] auto operator==(const Kit_layout_handle_set& other) const -> bool = default;
};

[[nodiscard]] auto get_layout_handle_set(const Kit_layout_pattern& pattern) -> Kit_layout_handle_set;

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
    std::vector<Kit_region_handle>         region_handles; // "Layout handles": the handles of a line, grid, ring or scatter

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

// The values of the handle moved to point (the node's frame), clamped to their ranges, the other values kept; a ring's
// arc takes the turn nearest the drag's last unclamped arc.
void apply_region_handle(
    const Kit_region_handle& handle,
    const glm::vec3&         point,
    Kit_layout_handle_drag&  in_out_drag,
    Kit_layout_pattern&      in_out_pattern
);

[[nodiscard]] auto get_arc_point(const Kit_layout_arc& arc, float degrees) -> glm::vec3;

} // namespace editor
