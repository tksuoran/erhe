#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

// Copies the geometry and (re)computes smooth vertex normals from the
// positions, publishing them where SHADING reads normals: vertex_normal
// (and vertex_normal_smooth for the edge-line slot), clearing any
// per-corner / per-facet normals that would outrank it in the
// renderable-mesh build's corner_normal > facet_normal > vertex_normal
// resolution. Topology, positions and all other attributes are
// unchanged; existing texture coordinates are preserved (unlike the
// subdivision operations' full post-processing, no facet texture
// coordinates are generated).
void smooth_normals(const Geometry& source, Geometry& destination);

} // namespace erhe::geometry::operation
