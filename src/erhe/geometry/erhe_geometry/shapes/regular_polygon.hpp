#pragma once

namespace GEO { class Mesh; }

namespace erhe::geometry::shapes {

void make_triangle (GEO::Mesh& mesh, float radius);
void make_quad     (GEO::Mesh& mesh, float edge);
void make_rectangle(GEO::Mesh& mesh, float width, float height, bool front_face = true, bool back_face = true);

} // namespace erhe::geometry::shapes
