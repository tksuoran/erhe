#pragma once

#include "erhe_geometry/operation/post_processing.hpp"

#include <geogram/basic/numeric.h>

#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// Catmull-Clark subdivision. When selected_facets is nullptr the whole mesh is
// subdivided (the classic behavior). When a facet set is given, only those facets
// are subdivided; the selection-boundary vertices are pinned and the unselected
// facets adjacent to the boundary are re-emitted as n-gons that splice in the new
// boundary-edge midpoints, so the subdivided region stays watertight with the rest.
// Iterated chains pass Post_processing::structural_only for intermediate
// iterations (see post_processing.hpp); the final iteration keeps full_default.
void catmull_clark_subdivision(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets = nullptr, Component_remap* remap = nullptr, Post_processing post_processing_level = Post_processing::full_default);

} // namespace erhe::geometry::operation
