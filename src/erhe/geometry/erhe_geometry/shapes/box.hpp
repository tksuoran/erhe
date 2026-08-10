#pragma once

#include <geogram/mesh/mesh.h>

namespace erhe::geometry::shapes {

void make_box(GEO::Mesh& mesh, float x_size, float y_size, float z_size);
void make_box(GEO::Mesh& mesh, float min_x, float max_x, float min_y, float max_y, float min_z, float max_z);
void make_box(GEO::Mesh& mesh, float r);
// subdivisions = number of interior subdivision planes per axis (>= 0):
// 0 = vertices only at the min/max corners of that axis (one cell),
// n = n + 1 cells. p is the superellipse-style power shaping the vertex
// distribution toward the faces.
void make_box(GEO::Mesh& mesh, GEO::vec3f size, GEO::vec3i subdivisions, float p = 1.0);

} // namespace erhe::geometry::shapes
