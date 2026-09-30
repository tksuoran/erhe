#pragma once

#include <geogram/basic/geometry.h>
#include <geogram/basic/numeric.h>

#include <set>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Smooth_vertices_options
{
public:
    float factor{0.5f}; // 0 keeps a vertex, 1 moves it onto its neighbours' average
    int   repeat{1};    // iterations
};

// Smooth vertices (Blender Vertex > Smooth Vertices, doc/plans/mesh_modeling.md
// catalog M10). Each iteration moves every selected vertex by factor toward
// the average of its edge-connected neighbours (the vertices before and after
// it in each facet), using the positions from the start of the iteration, so
// the result does not depend on the order of the selection. Boundary vertices
// are smoothed the same way; a vertex without neighbours stays. The geometry
// is not changed: out_positions receives the new positions of the selected
// vertices, in the order of the set (positions only, topology unchanged); a
// vertex index out of range is skipped.
void smooth_vertices(
    const Geometry&               geometry,
    const std::set<GEO::index_t>& selected_vertices,
    Smooth_vertices_options       options,
    std::vector<GEO::vec3f>&      out_positions
);

} // namespace erhe::geometry::operation
