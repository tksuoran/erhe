#pragma once

#include <geogram/basic/numeric.h>

#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// Conway "join": replace every facet with a fan of centroid kites - each interior
// edge becomes a quad spanning the two adjacent facet centroids and the edge
// endpoints (the original edge becomes the quad's diagonal). When selected_facets is
// nullptr the whole mesh is processed (the classic behavior). When a facet set is
// given, only those facets are joined: an edge interior to the selection (both
// adjacent facets selected) becomes the straddling quad, an edge on the selection
// boundary (one adjacent facet selected) becomes a triangle on the selected side
// that keeps the original boundary edge intact, and the unselected facets are copied
// through unchanged - so the result stays watertight.
void join(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets = nullptr, Component_remap* remap = nullptr);

} // namespace erhe::geometry::operation
