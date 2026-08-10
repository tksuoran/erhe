#pragma once

#include <geogram/basic/numeric.h>

#include <cstdint>
#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// sqrt(3) subdivision. When selected_facets is nullptr the whole mesh is subdivided
// (the classic behavior: every vertex is smoothed and every interior edge is
// flipped across the two facet centroids). When a facet set is given, only those
// facets are subdivided; an interior-to-selection edge is flipped as usual, a
// selection-boundary edge keeps its original segment (a centroid fan triangle on the
// selected side) so the unselected neighbor welds to it, selection-boundary vertices
// are pinned, and the unselected facets are copied through. The selected region stays
// watertight with the rest of the mesh.
// post_process_flags / regeneration_flags: exact Geometry::process_flag_* sets,
// see catmull_clark_subdivision.hpp (pass post_process_flags again as
// regeneration_flags for a standalone call).
void sqrt3_subdivision(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets, Component_remap* remap, uint64_t post_process_flags, uint64_t regeneration_flags);

} // namespace erhe::geometry::operation
