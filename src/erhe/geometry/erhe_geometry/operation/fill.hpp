#pragma once

#include <geogram/basic/numeric.h>

#include <vector>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;
class Geometry_component_selection;

// Fill (F) of doc/plans/mesh_modeling.md section 4.10 (catalog M15), composed
// on an Edit_mesh scratch (erhe_geometry/edit_mesh.hpp). Blender's contextual
// create is the behaviour reference.
//
// The selection is read in any mode: the selected vertices, the selected
// edges plus every existing edge between two selected vertices, and the
// facets of get_selection_facets() (split_components.hpp). A usable edge is
// a selected edge with fewer than two facets (a new facet next to a manifold
// edge would make it non-manifold). The cases run in order, stopping at the
// first that creates anything:
//
// 1. Exactly two selected vertices, no usable edge: erhe meshes have no wire
//    edges, so the edge between them is made only when it closes a facet -
//    the shortest chain of boundary edges between the two vertices (at least
//    two edges) plus the new edge becomes one n-gon. Without such a chain
//    there is nothing to fill.
// 2. One free vertex (a selected vertex on no usable edge) plus the usable
//    edges forming one open chain: the chain and the free vertex become one
//    n-gon (the free vertex connected to both chain ends; never a lone edge).
// 3. Usable edges: when every vertex of them has at most two usable edges,
//    each closed cycle becomes an n-gon, after one open chain is closed
//    between its ends or two open chains are joined end to end into one
//    loop, of the two ways the one with the larger area vector (the more
//    planar closure, never a bow-tie). Otherwise (a branching edge net, or
//    more than two open chains) the net is filled: every usable edge, whose
//    new facet must traverse it opposite to its one facet, takes the
//    shortest directed chain of usable edges closing a cycle through it
//    (a breadth-first front) that is not already a facet.
// 4. Selected facets: each edge-connected group joins into one facet
//    (Edit_mesh::join_facets(), as dissolve_faces() does).
// 5. Three or more selected vertices: sorted by angle around their centroid
//    in their fitted plane (least squares), they become one n-gon.
//
// A new facet's winding follows its neighbours: it traverses each existing
// edge it shares opposite to the neighbour's facet (a majority vote; a tie
// orients it along the mean normal of the facets at its vertices). Its
// corner at each vertex copies the corner provenance of a neighbouring facet
// (a facet on one of the new facet's edges at that vertex, else any facet at
// the vertex), so corner attributes such as texture coordinates carry over;
// its facet provenance is the first neighbouring facet's. A candidate facet
// that repeats an existing facet's vertex set, or that uses an edge that has
// two facets, is not created.
//
// With nothing to fill the destination is the unchanged source, and the
// result says so (logged). The remap (both pointers set) selects the new
// facets with their vertices and edges.

class Fill_result
{
public:
    std::vector<GEO::index_t> new_facets; // destination facets created (or joined) by the fill
    bool                      filled{false};
};

void fill_selection(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    Fill_result*                        result = nullptr,
    Component_remap*                    remap  = nullptr
);

} // namespace erhe::geometry::operation
