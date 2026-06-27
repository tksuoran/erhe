#pragma once

#include <geogram/basic/numeric.h>

#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

// sqrt(3) subdivision. When selected_facets is nullptr the whole mesh is subdivided
// (the classic behavior: every vertex is smoothed and every interior edge is
// flipped across the two facet centroids). When a facet set is given, only those
// facets are subdivided; an interior-to-selection edge is flipped as usual, a
// selection-boundary edge keeps its original segment (a centroid fan triangle on the
// selected side) so the unselected neighbor welds to it, selection-boundary vertices
// are pinned, and the unselected facets are copied through. The selected region stays
// watertight with the rest of the mesh.
void sqrt3_subdivision(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets = nullptr);

} // namespace erhe::geometry::operation
