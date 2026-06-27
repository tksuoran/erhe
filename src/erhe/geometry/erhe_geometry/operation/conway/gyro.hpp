#pragma once

#include <geogram/basic/numeric.h>

#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

// Conway "gyro": split every edge into two points (at ratio and 1 - ratio) and fan
// each facet into pentagons around its centroid. When selected_facets is nullptr the
// whole mesh is processed (the classic behavior). When a facet set is given, only
// those facets are gyrated; gyro keeps both edge split points on the original
// segment, so an active selection only restricts which edges are split, which facets
// get a centroid, and which facets are fanned. The unselected facets adjacent to the
// boundary are re-emitted as n-gons that splice in BOTH boundary-edge split points
// (multi-split weld), so the result stays watertight.
void gyro(const Geometry& source, Geometry& destination, float ratio = 1.0f / 3.0f, const std::set<GEO::index_t>* selected_facets = nullptr);

} // namespace erhe::geometry::operation
