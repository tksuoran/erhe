#pragma once

#include <geogram/basic/numeric.h>

#include <set>
#include <utility>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;
class Geometry_component_selection;

// Subdivide edges of doc/plans/mesh_modeling.md section 4.5 (M8), composed
// on an Edit_mesh scratch (erhe_geometry/edit_mesh.hpp).
//
// Every selected edge is split `cuts` times, cut k (k = 0 .. cuts - 1) at
// (k + 1) / (cuts + 1) of the original edge. Then each facet that holds a
// split edge is filled by the pattern of its split edges, recorded before any
// split. Below, "run" is the list of cut vertices of one split edge in the
// facet's corner order, A[j] the j-th vertex of one run, c = cuts:
//
// - quad, two opposite split edges (runs A, B): A[j] connects to
//   B[c - 1 - j]; c + 1 quads.
// - quad, two adjacent split edges (A ending at, B starting from the shared
//   corner): A[j] connects to B[c - 1 - j] (nested around the corner), then
//   the corner before A connects to the corner after B; one triangle at the
//   shared corner, c quads, one triangle at the opposite corner.
// - quad, three split edges (A, M, B in corner order, M the middle one):
//   A[j] connects to B[c - 1 - j] as in the opposite case, cutting c quad
//   strips off; the strip left next to M is the polygon
//   A[c - 1], corner, M[0 .. c - 1], corner, B[0], and its cut vertices
//   connect to the nearer end of the last connecting edge: M[k] to
//   A[c - 1] for k < ceil(c / 2), to B[0] otherwise. No vertex is added and
//   every facet is a quad or a triangle; for c = 1 the quad becomes two quads
//   (the strip along A, and A[0], M[0], corner, B[0]) and one triangle
//   (A[0], corner, M[0]).
// - quad, four split edges: grid fill, a (c + 2) x (c + 2) vertex grid of
//   (c + 1)^2 quads. Interior vertices are new: position by the Coons patch
//   over the four runs (bilinear over the corners when no edge is smoothed),
//   vertex provenance bilinear over the four corner vertices; the facet is
//   split along the full edge net, so their corners take the net's
//   facet-local interpolation (Edit_mesh::split_facet_edgenet()).
// - triangle, one split edge: the cuts fan to the opposite corner; c + 1
//   triangles.
// - triangle, three split edges: a triangular lattice of (c + 1)^2
//   triangles, interior vertices barycentric over the corners (position and
//   provenance), split along the full edge net.
// - any facet (triangles and quads included) with exactly two split edges
//   and no pattern above: A[j] connects to B[c - 1 - j], a pair already
//   joined by an edge or not sharing a facet is skipped.
// - anything else keeps the new vertices on its boundary.
//
// A facet in which two split edges share a vertex and are collinear
// (|dot| > 1 - 5e-5 of their directions) is not filled. With only_quads a
// facet that is not a quad is not filled (its edges are still split).
//
// Smoothness moves each cut vertex from its straight position toward a cubic
// Hermite curve between the edge's endpoints whose end tangents are the edge
// vector projected onto each endpoint's tangent plane (the plane normal to the
// endpoint's source vertex normal: vertex_normal, else vertex_normal_smooth,
// else the mean of the vertex's facet normals); the offset is
// smoothness * (curve - straight), zero at the endpoints and largest at the
// edge middle. A flat surface keeps its cuts on the edge.

class Subdivide_edges_options
{
public:
    int   cuts      {1};     // clamped to [1, 500]
    float smoothness{0.0f};  // 0: cuts on the straight edge
    bool  only_quads{false}; // fill only quads
};

// The "inner" elements the operation created, in destination indices: the cut
// vertices and the grid / lattice interior vertices, the edges the fills
// created (vertex pairs, first < second), and every facet that a fill split
// (all its pieces). The halves of the split edges are not inner.
class Subdivide_edges_result
{
public:
    std::vector<GEO::index_t>                          inner_vertices;
    std::vector<std::pair<GEO::index_t, GEO::index_t>> inner_edges;
    std::vector<GEO::index_t>                          inner_facets;
};

// The edges a component selection names for subdivision: its edges, the
// edges of its facets and every edge between two of its vertices, as source
// vertex pairs (first < second). out_edges is cleared first.
void get_selection_edges(
    const Geometry&                                   geometry,
    const Geometry_component_selection&               selection,
    std::set<std::pair<GEO::index_t, GEO::index_t>>&  out_edges
);

// selected_edges are source vertex pairs (either order); a pair that is not
// an edge of the source is ignored. result (optional) is cleared and filled.
// With a non-null remap (both pointers set) the source selection is carried
// to the result by the general remap: a selected edge maps to its split
// halves.
void subdivide_edges(
    const Geometry&                                        source,
    Geometry&                                              destination,
    const std::set<std::pair<GEO::index_t, GEO::index_t>>& selected_edges,
    Subdivide_edges_options                                options = {},
    Subdivide_edges_result*                                result  = nullptr,
    Component_remap*                                       remap   = nullptr
);

} // namespace erhe::geometry::operation
