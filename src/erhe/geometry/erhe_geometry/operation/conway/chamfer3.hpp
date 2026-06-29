#pragma once

#include <geogram/basic/numeric.h>

#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// Conway "chamfer": shrink every facet toward its centroid (by bevel_ratio) and
// replace every edge with a hexagonal face spanning the two shrunk facets. When
// selected_facets is nullptr the whole mesh is chamfered (the classic behavior).
// When a facet set is given, only those facets are chamfered: the selected facets
// shrink, edges interior to the selection become hexagons, edges on the selection
// boundary become bevel quads filling the gap to the unchanged neighbor, and the
// selection-boundary / exterior vertices are pinned so the result stays welded to
// the unmodified remainder (no cracks, no T-junctions). Chamfer inserts no edge
// midpoints, so the unselected facets are re-emitted 1:1.
void chamfer3(const Geometry& source, Geometry& destination, float bevel_ratio = 0.25f, const std::set<GEO::index_t>* selected_facets = nullptr, Component_remap* remap = nullptr);

} // namespace erhe::geometry::operation
