#pragma once

#include <geogram/basic/numeric.h>

#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

// Conway "ortho"-style quad subdivision: split every edge at its midpoint and
// connect the midpoints to the facet centroid. When selected_facets is nullptr the
// whole mesh is subdivided (the classic behavior). When a facet set is given, only
// those facets are subdivided; because subdivide never moves a vertex or edge
// midpoint off the original geometry there is no boundary smoothing to suppress -
// the unselected facets adjacent to the boundary are simply re-emitted as n-gons
// that splice in the new boundary-edge midpoints, so the result stays watertight.
void subdivide(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets = nullptr);

} // namespace erhe::geometry::operation
