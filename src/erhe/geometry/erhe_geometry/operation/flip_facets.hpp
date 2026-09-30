#pragma once

#include <geogram/basic/numeric.h>

#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// Normal operations of doc/plans/mesh_modeling.md catalog M10, composed on an
// Edit_mesh scratch (Edit_mesh::reverse_facet()). The facet sets name source
// facets. The result keeps every vertex, edge and facet index of the source;
// a flipped facet's corners run in the opposite order, each corner keeping
// its vertex and its attributes (texture coordinates, colors). A flipped
// facet's corner normals are negated, a vertex whose facets are all flipped
// has its vertex normal negated, and smooth vertex normals are recomputed.
// With a non-null remap (both pointers set) the source selection is carried
// to the result unchanged.

// Reverses the winding of the selected facets (Blender Mesh > Normals >
// Flip). An empty set leaves the mesh unchanged.
void flip_facets(
    const Geometry&               source,
    Geometry&                     destination,
    const std::set<GEO::index_t>& selected_facets,
    Component_remap*              remap = nullptr
);

enum class Normal_side : unsigned int
{
    outside,
    inside
};

// Recalculate normals (Blender Shift+N; Ctrl+Shift+N for inside). For each
// region of selected facets connected through edges with exactly two
// facets, both selected: the facet whose centre lies farthest from the
// region's area-weighted centre along the facet's own normal line (maximal
// |dot(centre - region centre, normal)|, the first such facet on ties) is
// oriented so its normal points away from the region centre; the winding
// then propagates across the region's shared edges (a neighbour that
// traverses a shared edge in the same direction as its already oriented
// facet is flipped). Normal_side::inside inverts every facet of that
// result. An empty set means every facet of the mesh.
void recalculate_facet_normals(
    const Geometry&               source,
    Geometry&                     destination,
    const std::set<GEO::index_t>& selected_facets,
    Normal_side                   side,
    Component_remap*              remap = nullptr
);

} // namespace erhe::geometry::operation
