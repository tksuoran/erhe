#pragma once

#include <geogram/basic/numeric.h>

#include <vector>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;
class Geometry_component_selection;

// Bridge edge loops of doc/plans/mesh_modeling.md section 4.10 (catalog M14),
// composed on an Edit_mesh scratch (erhe_geometry/edit_mesh.hpp). Blender's
// bridge edge loops is the behaviour reference.
//
// Loops: with selected facets, the facets are deleted (Delete_context::faces)
// and the boundaries of the deleted regions (the edges between a deleted and a
// kept facet) are the loops; otherwise the selected edges plus every edge
// between two selected vertices. A loop is a vertex sequence, closed when it
// cycles. A vertex with more than two loop edges, an edge with two facets (a
// bridge facet would make it non-manifold), or fewer than two loops is an
// error: the result is the unchanged source (logged as a warning).
//
// The loops are ordered by proximity (from the first, the nearest centre
// next) and paired by Bridge_connection. A pair of one open and one closed
// loop, or a pair whose loop edges already carry two facets (a loop bridged
// by an earlier pair), is skipped with a warning.
//
// Pairing within a loop pair (A, B):
// - Direction: open loops flip B when its end-to-end direction opposes A's;
//   closed loops flip B when the loop normals (Newell) point to opposite
//   sides along the vector between the loop centres. When that test is
//   indecisive (directions or normals nearly perpendicular to it, as for two
//   holes in one plane), B is flipped so that the facets along A and along B
//   traverse their loops in opposite directions, which a consistently wound
//   bridge needs; failing that too, closed loops compare the normals directly.
// - Closed loops: B is rotated to the start minimizing the sum of the paired
//   distances, then by twist_offset.
// - Unequal counts: the shorter loop repeats entries (every entry doubled while
//   that stays within the longer count, the rest spread evenly); a repeated
//   entry yields a triangle. The strip is beautified: two neighbouring
//   triangles facing opposite loops exchange their shared rung (the edge
//   between the loops) when that shortens it. Consecutive triangles facing
//   opposite loops then join back into quads.
// - One facet per step (a quad, or a triangle at a repeat). The winding is
//   voted over the loop edges: a bridge facet traverses each loop edge
//   opposite to the facet that had it before the bridge. Each corner copies
//   the corner of that adjacent facet on the same loop edge; the facet
//   provenance is the adjacent facet's on loop A.
// - merge (equal counts only, else an error): no facets; each pair welds at
//   lerp(a, b, merge_factor) (the weld core, Edit_mesh::weld_vertices()),
//   vertex attributes blended by the factor.
// - cuts > 0 (without merge): the rungs (the edges between the loops) are
//   subdivided as an edge ring (subdivide_edges(), section 4.5), so each
//   bridge quad becomes cuts + 1 quads.
//
// The remap (both pointers set) selects the bridge facets with their vertices
// and edges (merge: the welded vertices and the edges between them).

enum class Bridge_connection : unsigned int
{
    open_loop,   // consecutive loops bridged as one open chain: 1-2, 2-3, ...
    closed_loop, // as open_loop, and the last loop to the first (three or more loops)
    loop_pairs   // 1-2, 3-4, ...
};

class Bridge_loops_options
{
public:
    Bridge_connection connection  {Bridge_connection::open_loop};
    bool              merge       {false};
    float             merge_factor{0.5f}; // 0: at loop A, 1: at loop B
    int               twist_offset{0};    // closed loops: extra rotation of loop B
    int               cuts        {0};    // 0 .. 500
};

class Bridge_loops_result
{
public:
    std::vector<GEO::index_t> bridge_facets; // destination facets of the bridge (all pieces with cuts)
};

void bridge_loops(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    Bridge_loops_options                options = {},
    Bridge_loops_result*                result  = nullptr,
    Component_remap*                    remap   = nullptr
);

} // namespace erhe::geometry::operation
