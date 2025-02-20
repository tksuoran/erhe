#pragma once

namespace GEO { class Mesh; };

namespace erhe::geometry::shapes {

void make_torus(GEO::Mesh& mesh, float major_radius, float minor_radius, int major_axis_steps, int minor_axis_steps);

} // namespace erhe::geometry::shapes
