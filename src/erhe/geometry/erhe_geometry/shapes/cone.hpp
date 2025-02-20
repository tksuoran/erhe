#pragma once

namespace GEO { class Mesh; }

namespace erhe::geometry::shapes {

void make_conical_frustum(
    GEO::Mesh& mesh,
    float      min_x,
    float      max_x,
    float      bottom_radius,
    float      top_radius,
    bool       use_bottom,
    bool       use_top,
    int        slice_count,
    int        stack_division
);

// Cone is special of conical frustum which is not cut from top
// top is at max_x, bottom is at min_x
void make_cone(
    GEO::Mesh& mesh,
    float      min_x,
    float      max_x,
    float      bottom_radius,
    bool       use_bottom,
    int        slice_count,
    int        stack_division
);

// Cylinder is special of conical frustum which has top radius equal to bottom radius
void make_cylinder(
    GEO::Mesh& mesh,
    float      min_x,
    float      max_x,
    float      radius,
    bool       use_bottom,
    bool       use_top,
    int        slice_count,
    int        stack_division
);

} // namespace erhe::geometry::shapes
