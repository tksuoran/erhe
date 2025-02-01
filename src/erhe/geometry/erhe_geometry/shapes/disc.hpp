#pragma once

namespace GEO { class Mesh; }

namespace erhe::geometry::shapes {

void make_disc(GEO::Mesh& mesh, double outer_radius, double inner_radius, int slice_count, int stack_count);

void make_disc(
    GEO::Mesh& mesh, 
    double     outer_radius,
    double     inner_radius,
    int        slice_count,
    int        stack_count,
    int        slice_begin,
    int        slice_end,
    int        stack_begin,
    int        stack_end
);

} // namespace erhe::geometry::shapes
