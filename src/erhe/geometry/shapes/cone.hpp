#pragma once

#include "erhe/geometry/geometry.hpp"

namespace erhe::geometry::shapes
{

[[nodiscard]] auto make_conical_frustum(
    double min_x,
    double max_x,
    double bottom_radius,
    double top_radius,
    bool   use_bottom,
    bool   use_top,
    int    slice_count,
    int    stack_division
) -> Geometry;

// Cone is special of conical frustum which is not cut from top
// top is at max_x, bottom is at min_x
[[nodiscard]] auto make_cone(
    double min_x,
    double max_x,
    double bottom_radius,
    bool   use_bottom,
    int    slice_count,
    int    stack_division
) -> Geometry;

// Cylinder is special of conical frustum which has top radius equal to bottom radius
[[nodiscard]] auto make_cylinder(
    double min_x,
    double max_x,
    double radius,
    bool   use_bottom,
    bool   use_top,
    int    slice_count,
    int    stack_division
) -> Geometry;

} // namespace erhe::geometry::shapes
