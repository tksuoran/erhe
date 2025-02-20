#pragma once

#include <geogram/mesh/mesh.h>

namespace erhe::geometry::shapes {

void make_box(GEO::Mesh& mesh, float x_size, float y_size, float z_size);
void make_box(GEO::Mesh& mesh, float min_x, float max_x, float min_y, float max_y, float min_z, float max_z);
void make_box(GEO::Mesh& mesh, float r);
void make_box(GEO::Mesh& mesh, GEO::vec3f size, GEO::vec3i div, float p = 1.0);

} // namespace erhe::geometry::shapes
