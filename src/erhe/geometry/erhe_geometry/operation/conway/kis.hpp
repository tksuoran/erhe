#pragma once

#include <geogram/basic/numeric.h>

#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// Conway "kis": raise a centroid over each facet and fan it into triangles
// (centroid, corner, next corner). When selected_facets is nullptr the whole mesh
// is processed (the classic behavior). When a facet set is given, only those facets
// are raised; kis keeps every original edge intact (it never inserts edge
// midpoints), so the selected fan triangles share their boundary edges with the
// untouched neighbors - the unselected facets are simply copied through and the
// result stays watertight with no T-junctions.
void kis(const Geometry& source, Geometry& destination, float height = 0.0f, const std::set<GEO::index_t>* selected_facets = nullptr, Component_remap* remap = nullptr);

} // namespace erhe::geometry::operation
