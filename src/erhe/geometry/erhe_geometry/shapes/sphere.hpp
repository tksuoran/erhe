#pragma once

namespace GEO { class Mesh; }

namespace erhe::geometry::shapes {

void make_sphere(GEO::Mesh& mesh, double radius, unsigned int slice_count, unsigned int stack_division);

} // namespace erhe::geometry::shapes
