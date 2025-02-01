#pragma once

namespace GEO { class Mesh; }

namespace erhe::geometry::shapes {

void make_triangle (GEO::Mesh& mesh, double radius);
void make_quad     (GEO::Mesh& mesh, double edge);
void make_rectangle(GEO::Mesh& mesh, double width, double height, bool front_face = true, bool back_face = true);

} // namespace erhe::geometry::shapes
