#pragma once

#include "erhe/geometry/geometry.hpp"

namespace erhe::geometry::shapes
{

[[nodiscard]] auto make_conical_frustum(
    const double min_x,
    const double max_x,
    const double bottom_radius,
    const double top_radius,
    const bool   use_bottom,
    const bool   use_top,
    const int    slice_count,
    const int    stack_division
) -> Geometry;

// Cone is special of conical frustum which is not cut from top
// top is at max_x, bottom is at min_x
[[nodiscard]] auto make_cone(
    const double min_x,
    const double max_x,
    const double bottom_radius,
    const bool   use_bottom,
    const int    slice_count,
    const int    stack_division
) -> Geometry;

// Cylinder is special of conical frustum which has top radius equal to bottom radius
[[nodiscard]] auto make_cylinder(
    const double min_x,
    const double max_x,
    const double radius,
    const bool   use_bottom,
    const bool   use_top,
    const int    slice_count,
    const int    stack_division
) -> Geometry;

} // namespace erhe::geometry::shapes
