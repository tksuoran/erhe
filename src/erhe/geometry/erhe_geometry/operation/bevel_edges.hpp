#pragma once

#include <geogram/basic/geometry.h>
#include <geogram/basic/numeric.h>

#include <set>
#include <utility>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// Bevel edges, first version (M13a) of doc/plans/mesh_modeling.md section
// 4.9, composed on an Edit_mesh scratch (erhe_geometry/edit_mesh.hpp):
// edges only, one segment, offset types offset and width, loop slide.
// Blender's bevel is the behaviour reference.
//
// A selected edge is beveled when it has exactly two facets that traverse it
// in opposite directions; a boundary edge (one facet) or a non-manifold edge
// is skipped with a warning. Every vertex of a beveled edge (a bevel vertex)
// needs its facets to form one fan (closed, or open at the mesh boundary; a
// closed fan of at least three facets) with the vertex once in each facet;
// otherwise the beveled edges at it are skipped with a warning.
//
// Around a bevel vertex v the fan is ordered counter-clockwise about the
// outward normal: ring edges e_0, e_1, ... and wedges W_j (the facet between
// e_j and e_j+1). A beveled edge has an offset line in each of its two
// facets: the edge's line moved into the facet, perpendicular to the edge in
// the facet plane, by the offset (offset type `offset`: the amount; `width`:
// amount / (2 sin(phi / 2)), phi the angle between the edge's two in-facet
// perpendiculars, so the bevel facet is `amount` wide at a right angle).
// The wedges between two consecutive beveled edges (a span) share one
// boundary vertex, which replaces v in each of them:
// - no unbeveled edge between: the meet of the two offset lines in their
//   shared facet;
// - one unbeveled edge between, loop slide on: on that edge, at the mean of
//   the points where the two offset lines come closest to it;
// - otherwise: the midpoint of the closest approach of the two offset lines
//   (their meet when they intersect);
// - an end span of an open fan (a mesh boundary edge at its end): where the
//   offset line comes closest to the unbeveled edge next to the beveled one.
// A closed fan with a single beveled edge e_i gets a boundary vertex on each
// of its other edges: on e_i+1 and e_i-1 where the offset lines come closest
// to them, on every edge further around at the amount along the edge; the
// wedges W_i and W_i-1 take one of them, every other wedge the two on its
// edges.
//
// The result: every facet with a corner at a bevel vertex is rebuilt with
// that corner replaced by the chain of its wedge's boundary vertices; each
// beveled edge becomes one quad (an edge facet) between the boundary vertices
// at its two ends in its two facets; the boundary vertices of a bevel vertex
// in ring order form one vertex facet when there are three or more (two: the
// edge facets meet directly). The bevel vertices and beveled edges are
// deleted.
//
// Provenance. A boundary vertex copies its bevel vertex. Its corner in a
// rebuilt facet interpolates that facet's original corners: linearly between
// the corner at the bevel vertex and the corner at the far end of the edge it
// lies on (by the fraction of that edge's length), or by mean value
// coordinates in the facet's original plane when it lies on no edge of the
// facet. An edge facet's corners copy the original corners of its two facets
// at the matching ends, and its source facet is the facet on its right. A
// vertex facet's corners average every original corner at the bevel vertex,
// its source facet is the first facet of the fan.
//
// A selection without a beveled edge leaves the mesh unchanged.

enum class Bevel_offset_type : unsigned int
{
    offset = 0, // the amount is the distance of each offset line from its edge
    width  = 1  // the amount is the width of the bevel facet
};

class Bevel_edges_options
{
public:
    Bevel_offset_type offset_type{Bevel_offset_type::offset};
    float             amount     {0.0f};
    bool              loop_slide {true};
};

// Destination indices. boundary_vertices are the new vertices, ascending;
// boundary_directions[i] is the direction of boundary_vertices[i] per unit
// amount, so it sits at (its bevel vertex's original position) +
// (amount * boundary_directions[i]). Every placement rule above is linear in
// the amount, so the directions are exact for offset types `offset` and
// `width` at fixed topology: a caller drives the amount by moving the
// vertices along them without re-running the operation. edge_facets are the
// edge facets (one per beveled edge), vertex_facets the vertex facets, both
// ascending.
class Bevel_edges_result
{
public:
    std::vector<GEO::index_t> boundary_vertices;
    std::vector<GEO::vec3f>   boundary_directions;
    std::vector<GEO::index_t> edge_facets;
    std::vector<GEO::index_t> vertex_facets;
};

// selected_edges are source vertex pairs (either order); a pair that is not
// an edge of the source is ignored. result (optional) is cleared and filled.
// With a non-null remap (both pointers set) the destination selection is the
// edge facets with their edges and vertices (Blender selects the new bevel
// facets); the source selection is not carried.
void bevel_edges(
    const Geometry&                                        source,
    Geometry&                                              destination,
    const std::set<std::pair<GEO::index_t, GEO::index_t>>& selected_edges,
    Bevel_edges_options                                    options = {},
    Bevel_edges_result*                                    result  = nullptr,
    Component_remap*                                       remap   = nullptr
);

} // namespace erhe::geometry::operation
