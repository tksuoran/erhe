#pragma once

#include <geogram/basic/numeric.h>

#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// Conway "meta": split every edge at its midpoint and fan each facet from its
// centroid through the corners and edge midpoints (two triangles per corner).
// When selected_facets is nullptr the whole mesh is processed (the classic
// behavior). When a facet set is given, only those facets are split; because meta
// never moves a vertex or edge midpoint off the original geometry there is no
// boundary smoothing to suppress - the unselected facets adjacent to the boundary
// are re-emitted as n-gons that splice in the new boundary-edge midpoints, so the
// result stays watertight.
void meta(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets = nullptr, Component_remap* remap = nullptr);

} // namespace erhe::geometry::operation
