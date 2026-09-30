#pragma once

#include <geogram/basic/geometry.h>
#include <geogram/basic/numeric.h>

#include <cstddef>
#include <set>
#include <utility>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// Bevel edges (doc/plans/mesh_modeling.md section 4.9, M13a and the
// segments and profile of M13b), composed on an Edit_mesh scratch
// (erhe_geometry/edit_mesh.hpp): edges only, offset types offset and width,
// loop slide, segments and profile. Blender's bevel is the behaviour
// reference.
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
// Segments and profile. With segments n > 1 each end of a beveled edge
// gets a profile: n + 1 samples from the edge facet's corner in its right
// facet (B_r) to the one in its left facet (B_l), the n - 1 interior ones new
// vertices. With V the bevel vertex, C = B_r + B_l - V (the meet of the two
// facets' offset lines in the plane of V, B_r and B_l) and (x, y) a point of
// the unit superellipse |x|^r + |y|^r = 1 from (1, 0) to (0, 1), sample k
// sits at C + x (B_r - C) + y (B_l - C) = V + (1 - y)(B_r - V) + (1 - x)(B_l - V).
// The exponent is r = 1 / (1 - profile): profile 0 is the straight chamfer
// (r = 1, the one segment line), 0.5 the quarter circle (r = 2, a true circle
// when B_r - V and B_l - V are perpendicular and equally long), 1 the square
// corner (r infinite: the samples lie on the two legs B_r - V - B_l, on the
// original facets; an even count puts the middle sample at V). The samples
// are spaced evenly by arc length on the unit curve and symmetric, so the
// profile from B_l to B_r is the same vertices in reverse. Blender's concave
// profiles (below its 0.25) are not offered.
// - Each beveled edge becomes a strip of n quads between the matching samples
//   of its two end profiles (the edge facets).
// - A rebuilt facet whose chain holds both ends of a profile (a single
//   beveled edge at a three-valent vertex: the facet across the vertex) gets
//   the profile's samples between them.
// - At a vertex with exactly two beveled edges on a closed fan the two edges
//   share one profile (the strips continue through; no vertex facet).
// - A vertex facet of a vertex with fewer than three beveled edges (or an
//   open fan) gets the samples of the profiles along its sides.
// - At a vertex with three or more beveled edges on a closed fan, the vertex
//   patch is the cutoff pattern: side i of the one segment vertex facet is
//   now the profile Q_i[0 .. n] (Q_i[n] = Q_i+1[0] the shared boundary
//   vertex). With h = n / 2 (rounded down), the centre facet takes Q_i[h]
//   and, for odd n, Q_i[n - h] of every side, in ring order (k or 2k
//   corners); each boundary vertex Q_i[n] is filled up to the centre facet
//   by a triangle (Q_i[n - 1], Q_i[n], Q_i+1[1]) and h - 1 quads
//   (Q_i[n - j - 1], Q_i[n - j], Q_i+1[j], Q_i+1[j + 1]), j = 1 .. h - 1.
//   Blender's default, the adjacent-pattern grid subdivision (ADJ), is not
//   built.
// Profile vertices copy their bevel vertex's provenance; their corners in a
// rebuilt facet interpolate like a boundary vertex off the facet's edges
// (mean value coordinates); in a strip quad they blend the corners of the
// right and left facets at that end by the sample's index; in the vertex
// patch they average every original corner at the vertex, as the vertex
// facet does. The patch facets count as vertex facets.
//
// Edge sharpness. A beveled edge's sharpness is dropped with the edge; the
// new edges carry none, except that an unbeveled edge a boundary vertex lies
// on (a ring edge, shortened by the bevel) keeps its sharpness on the
// shortened edge. Edges away from the bevel keep theirs unchanged.
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
    int               segments   {1};    // clamped to 1 .. 1000; 1 is the one segment bevel
    float             profile    {0.5f}; // clamped to 0 .. 1; see "Segments and profile" above
};

// Destination indices. boundary_vertices are the new vertices (the boundary
// vertices and, with segments > 1, the profile samples), ascending;
// boundary_directions[i] is the direction of boundary_vertices[i] per unit
// amount, so it sits at (its bevel vertex's original position) +
// (amount * boundary_directions[i]). Every placement rule above is linear in
// the amount (a profile sample is a fixed combination of its two boundary
// vertices' directions), so the directions are exact for offset types
// `offset` and `width`, any segments and profile, at fixed topology: a
// caller drives the amount by moving the vertices along them without
// re-running the operation. edge_facets are the edge facets (segments per
// beveled edge), vertex_facets the vertex facets (with the cutoff patch
// facets), both ascending; beveled_edges counts the beveled edges.
class Bevel_edges_result
{
public:
    std::vector<GEO::index_t> boundary_vertices;
    std::vector<GEO::vec3f>   boundary_directions;
    std::vector<GEO::index_t> edge_facets;
    std::vector<GEO::index_t> vertex_facets;
    std::size_t               beveled_edges{0};
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
