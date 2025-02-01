#pragma once

namespace GEO { class Mesh; }

namespace erhe::geometry::shapes {

void make_conical_frustum(
    GEO::Mesh& mesh,
    double     min_x,
    double     max_x,
    double     bottom_radius,
    double     top_radius,
    bool       use_bottom,
    bool       use_top,
    int        slice_count,
    int        stack_division
);

// Cone is special of conical frustum which is not cut from top
// top is at max_x, bottom is at min_x
void make_cone(
    GEO::Mesh& mesh,
    double     min_x,
    double     max_x,
    double     bottom_radius,
    bool       use_bottom,
    int        slice_count,
    int        stack_division
);

// Cylinder is special of conical frustum which has top radius equal to bottom radius
void make_cylinder(
    GEO::Mesh& mesh,
    double     min_x,
    double     max_x,
    double     radius,
    bool       use_bottom,
    bool       use_top,
    int        slice_count,
    int        stack_division
);

} // namespace erhe::geometry::shapes
