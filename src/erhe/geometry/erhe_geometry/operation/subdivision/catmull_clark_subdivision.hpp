#pragma once

#include <geogram/basic/numeric.h>

#include <cstdint>
#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// Catmull-Clark subdivision. When selected_facets is nullptr the whole mesh is
// subdivided (the classic behavior). When a facet set is given, only those facets
// are subdivided; the selection-boundary vertices are pinned and the unselected
// facets adjacent to the boundary are re-emitted as n-gons that splice in the new
// boundary-edge midpoints, so the subdivided region stays watertight with the rest.
// post_process_flags is the exact Geometry::process_flag_* set the operation's
// post-processing runs; it must include at least connect + build_edges +
// compute_facet_centroids (Geometry_operation::structural_post_process_flags).
// regeneration_flags names the process flags whose regenerated channels are not
// interpolated from the source: pass post_process_flags for a standalone call.
// An iterated chain passes structural_post_process_flags as post_process_flags
// for the intermediate iterations and the FINAL iteration's flags as
// regeneration_flags throughout, so channels the final pass re-derives from
// positions are not pointlessly interpolated in between.
void catmull_clark_subdivision(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets, Component_remap* remap, uint64_t post_process_flags, uint64_t regeneration_flags);

} // namespace erhe::geometry::operation
