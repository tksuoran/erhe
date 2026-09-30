#pragma once

#include <geogram/basic/numeric.h>

#include <set>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;
class Geometry_component_selection;

// Connect vertex path (J) of doc/plans/mesh_modeling.md section 4.10 (catalog
// M16), composed on an Edit_mesh scratch (erhe_geometry/edit_mesh.hpp).
// Blender's connect vertex path is the behaviour reference. Every new edge
// splits a facet (Edit_mesh::split_facet()), so both halves keep the facet's
// provenance and the corners at the new edge copy the facet's corners.
// The remaps (both pointers set) select the new edges, the selected vertices
// and every vertex a connection inserted.

// Per facet: the corners at selected vertices in facet order, without a
// corner whose two neighbouring corners are both selected (the inside of a
// contiguous selected run); consecutive remaining corners (one pair when two
// remain) are split between, except a pair of adjacent corners, a pair whose
// chord midpoint lies outside the facet (projected onto its Newell plane:
// the split would leave the facet) and a pair crossing an earlier split of
// the facet.
void connect_vertices(
    const Geometry&               source,
    Geometry&                     destination,
    const std::set<GEO::index_t>& selected_vertices,
    Component_remap*              remap = nullptr
);

// Two vertices that need not share a facet: the cutting plane through both
// contains their normals' mean (each vertex normal the mean of its facets'
// Newell normals) projected perpendicular to the line between them (another
// perpendicular when the mean is parallel to the line or zero). A best-first
// search keyed by accumulated Euclidean length runs from a over elements -
// vertices on the plane and edges the plane crosses strictly between their
// endpoints (at the crossing point) - stepping from an element to every
// other element of each facet it touches, each element visited once, until
// b is reached. The crossed edges of the path are split at the plane and
// consecutive path vertices are connected across the facet they share.
// Without a path the destination is the unchanged source (logged).
void connect_vertex_pair(
    const Geometry&  source,
    Geometry&        destination,
    GEO::index_t     a,
    GEO::index_t     b,
    Component_remap* remap = nullptr
);

// The selection's vertices (its vertices and the endpoints of its edges):
// connect_vertex_pair() for exactly two vertices sharing no facet,
// connect_vertices() otherwise.
void connect_selection(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    Component_remap*                    remap = nullptr
);

} // namespace erhe::geometry::operation
