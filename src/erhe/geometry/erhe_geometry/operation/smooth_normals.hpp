#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

// Copies the geometry and (re)computes smooth vertex normals from the
// positions. Topology, positions and all other attributes are unchanged;
// existing texture coordinates are preserved (unlike the subdivision
// operations' full post-processing, no facet texture coordinates are
// generated).
void smooth_normals(const Geometry& source, Geometry& destination);

} // namespace erhe::geometry::operation
