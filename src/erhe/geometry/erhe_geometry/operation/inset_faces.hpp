#pragma once

#include <geogram/basic/geometry.h>
#include <geogram/basic/numeric.h>

#include <set>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// Inset faces of doc/plans/mesh_modeling.md section 4.8 (M7), composed on an
// Edit_mesh scratch (erhe_geometry/edit_mesh.hpp). Blender's inset is the
// behaviour reference.
//
// Region (individual off). The region is the selected facets (outset: every
// other facet). A boundary edge is an edge of a region facet that also has a
// facet outside the region, or that has no other facet (a mesh boundary edge)
// while `boundary` is on. Around each vertex of a boundary edge, the region
// facets split into fans: two region facets are in one fan when they share an
// edge at the vertex whose facets all lie in the region. Every fan with at
// least one boundary edge at the vertex gets its own inset vertex (a copy of
// the vertex, so a vertex where the region touches itself - a pinch - gets one
// inset vertex per fan), and the fan's facets move onto it; the facets outside
// the region keep the original vertex, however many fans they form (Blender
// glues those back together). The rim facets fill between each boundary edge,
// which stays in place, and its copy on the inset vertices: one quad per
// boundary edge (outer corners a, b, inner corners b', a'). A mesh boundary
// edge of a region facet that is not a boundary edge (boundary off) moves with
// the region and gets no rim facet.
//
// Each boundary edge holds the unit tangent in its region facet's plane,
// perpendicular to the edge and pointing into the facet. An inset vertex moves
// along:
// - two boundary edges at the vertex in its fan: the normalized sum of their
//   tangents; even offset scales it by 1 / cos(half angle) (the reciprocal of
//   its dot product with either tangent), so the inset edges lie at the
//   thickness from the boundary edges. With edge rail, a fan holding exactly
//   one edge at the vertex between two of its facets moves along that edge
//   instead (toward the neighbour vertex's original position), even offset
//   scaling it by the reciprocal of its mean dot product with the tangents.
// - one boundary edge (the fan ends at a mesh boundary edge that is not a
//   boundary edge, boundary off): along that unsplit mesh boundary edge, so
//   the inset stays flush with the mesh boundary; even offset scales it by the
//   reciprocal of its dot product with the tangent.
// - three or more boundary edges in one fan (only at a non-manifold vertex):
//   the normalized sum of all their tangents, even offset scaling it by the
//   reciprocal of the mean dot product with them.
// Relative offset further scales the direction by the mean length of the
// fan's boundary edges at the vertex. Every scaling reciprocal is taken of a
// cosine clamped to at least 0.1.
//
// Depth moves every vertex of the region facets (the inset vertices and the
// unsplit vertices inside the region) along the normalized sum of the normals
// of the region facets using it, scaled by the shell factor (the reciprocal of
// the mean dot product of that direction with those facet normals, clamped as
// above).
//
// Individual (individual on; boundary, outset and edge rail do not apply).
// Every selected facet gets its own copy of each of its vertices and a rim
// quad per edge. A copy moves along the normalized sum of the tangents of the
// facet's two edges at the corner, with the same even and relative scaling
// (relative: the mean length of those two edges), and depth moves it along the
// facet normal.
//
// Provenance. An inset vertex copies its vertex. A moved region facet keeps
// its source facet and corners; with interpolate on, the corners at moved
// vertices re-sample their provenance (so every corner attribute) from the
// facet's pre-move corners by mean value coordinates of the moved position in
// the facet's pre-move plane. A rim facet takes its region facet's source
// facet; its outer corners copy that facet's pre-move corners, its inner
// corners the facet's (re-sampled) corners. Each region facet edge's
// sharpness carries to its moved copy, and a boundary edge keeps its own.
//
// A region without boundary edges (every facet of a closed mesh selected)
// leaves the mesh unchanged.

class Inset_faces_options
{
public:
    bool  boundary       {true};
    bool  even_offset    {true};
    bool  relative_offset{false};
    bool  edge_rail      {false};
    float thickness      {0.0f};
    float depth          {0.0f};
    bool  outset         {false};
    bool  individual     {false};
    bool  interpolate    {true};
};

// Destination indices. inset_vertices are every vertex of the inset facets
// (the inset copies and, in region mode, the unsplit vertices inside the
// region), ascending. inset_directions[i] is the thickness direction of
// inset_vertices[i], already scaled by the even / relative factors (zero for an
// unsplit vertex), and depth_directions[i] its depth direction (scaled by the
// shell factor), so inset_vertices[i] sits at
// origin + (thickness * inset_directions[i]) + (depth * depth_directions[i]),
// where origin is its position with thickness and depth 0. inset_facets are the
// moved facets, rim_facets the rim facets, both ascending.
class Inset_faces_result
{
public:
    std::vector<GEO::index_t> inset_vertices;
    std::vector<GEO::vec3f>   inset_directions;
    std::vector<GEO::vec3f>   depth_directions;
    std::vector<GEO::index_t> inset_facets;
    std::vector<GEO::index_t> rim_facets;
};

// selected_facets are source facet indices; an index that is not a facet of
// the source is ignored. result (optional) is cleared and filled. With a
// non-null remap (both pointers set) the source selection is carried to the
// result by the general remap, without the rim facets: a selected facet maps to
// its moved facet (outset: to itself).
void inset_faces(
    const Geometry&               source,
    Geometry&                     destination,
    const std::set<GEO::index_t>& selected_facets,
    Inset_faces_options           options = {},
    Inset_faces_result*           result  = nullptr,
    Component_remap*              remap   = nullptr
);

} // namespace erhe::geometry::operation
