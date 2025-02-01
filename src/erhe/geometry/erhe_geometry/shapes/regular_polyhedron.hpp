#pragma once

namespace GEO { class Mesh; }

namespace erhe::geometry::shapes {

void make_cuboctahedron(GEO::Mesh& mesh, double radius);
void make_dodecahedron (GEO::Mesh& mesh, double radius);
void make_icosahedron  (GEO::Mesh& mesh, double radius);
void make_octahedron   (GEO::Mesh& mesh, double radius);
void make_tetrahedron  (GEO::Mesh& mesh, double radius);
void make_cube         (GEO::Mesh& mesh, double radius);

} // namespace erhe::geometry::shapes
