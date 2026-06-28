#pragma once

#include <geogram/basic/numeric.h>

#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// Conway "truncate": cut every corner off, replacing each vertex with a vertex-face
// and shrinking every facet. When selected_facets is nullptr the whole mesh is
// processed (the classic behavior). When a facet set is given, only the selected
// facets are truncated: vertices fully interior to the selection become vertex-faces
// (the original vertex is removed), boundary/unselected vertices are kept, and each
// selected facet's cut corner at a kept boundary vertex is filled with a small
// corner-cap triangle so the seam welds to the untouched neighbor. The unselected
// facets adjacent to the boundary are re-emitted with both interface-edge split
// points spliced in, so the result stays watertight.
void truncate(const Geometry& source, Geometry& destination, float ratio = 1.0f / 3.0f, const std::set<GEO::index_t>* selected_facets = nullptr, Component_remap* remap = nullptr);

} // namespace erhe::geometry::operation
