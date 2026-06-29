#pragma once

#include <geogram/basic/numeric.h>

#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// "Merge faces" (a.k.a. dissolve faces): replace each connected group of selected
// facets - connected via SHARED EDGES (not merely a shared vertex) - with a single
// polygon spanning the group's boundary loop, dropping the now-interior shared edges
// (and any vertex that becomes interior to a merged group). Facets outside the
// selection are left unchanged and the result stays welded watertight. A group whose
// boundary is not a single simple loop (it encloses a hole, or pinches at a vertex)
// cannot be represented as one simple polygon and is left unchanged. With
// selected_facets == nullptr every facet is "selected"; a closed component then has no
// boundary loop and is left unchanged (no-op).
void merge_faces(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets = nullptr, Component_remap* remap = nullptr);

} // namespace erhe::geometry::operation
