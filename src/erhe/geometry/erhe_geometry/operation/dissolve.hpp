#pragma once

#include "erhe_geometry/edit_mesh.hpp"

#include <geogram/basic/numeric.h>

#include <set>
#include <utility>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;
class Geometry_component_selection;

// Delete and dissolve operations of doc/plans/mesh_modeling.md section 4.3,
// composed on an Edit_mesh scratch (erhe_geometry/edit_mesh.hpp). The
// selection sets name source indices (edges as canonical vertex pairs).
// Each function writes its result into destination; with a non-null remap
// (both pointers set) the source selection is carried to the result through
// the provenance of the emission: a surviving vertex maps to itself, a
// facet to the facets descending from it (a joined facet descends from the
// first facet of its region), and an edge to its image when that edge
// still exists in the result (Geometry_operation::remap_component_selection()).

// Deletes one set of the selection: the vertices for Delete_context::vertices,
// the edges for Delete_context::edges and only_edges_and_faces, the facets
// for Delete_context::faces and only_faces (see Edit_mesh::delete_elements()).
void delete_components(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    Delete_context                      context,
    Component_remap*                    remap = nullptr
);

class Dissolve_faces_options
{
public:
    // Collapse the vertices that the joins left with exactly two edges
    // (vertices that had more than two edges before).
    bool dissolve_vertices{false};
};

// Joins each edge-connected group of the selected facets (connected through
// edges with exactly two facets, both selected) into one facet. A group
// whose join fails (non-manifold, boundary not one loop) is kept unchanged.
void dissolve_faces(
    const Geometry&               source,
    Geometry&                     destination,
    const std::set<GEO::index_t>& selected_facets,
    Dissolve_faces_options        options = {},
    Component_remap*              remap   = nullptr
);

class Dissolve_edges_options
{
public:
    // Collapse each end vertex of a joined edge that the joins left with
    // exactly two edges, except a vertex that touches an unselected facet
    // (a facet with an edge outside the selection) while having more than
    // one selected edge.
    bool  dissolve_vertices{true};

    // Keep the four corners of each selected edge's triangle pair, so
    // dissolving the edge between two triangles yields a quad.
    bool  preserve_quads{true};

    // A two-valent end vertex collapses only when the angle between its two
    // remaining edges - the plain angle between the direction into the
    // vertex and the direction out of it, 0 for collinear edges - is below
    // this threshold. (Blender blends this with the angle around the vertex
    // normal; this is the plain angle.) All angles are measured before any
    // collapse. 180 collapses every candidate, 0 keeps every one.
    float angle_threshold_degrees{180.0f};

    // Before joining, split off the corner of each vertex that is predicted
    // to collapse, in every facet with more than three corners whose two
    // neighbouring corners are not collapse candidates, so the surrounding
    // facets keep their shape.
    bool  face_split{false};
};

// Joins the facet pair of each selected manifold edge, then collapses
// vertices as the options state.
void dissolve_edges(
    const Geometry&                                        source,
    Geometry&                                              destination,
    const std::set<std::pair<GEO::index_t, GEO::index_t>>& selected_edges,
    Dissolve_edges_options                                 options = {},
    Component_remap*                                       remap   = nullptr
);

class Dissolve_vertices_options
{
public:
    // Before joining, split off the vertex's corner in every facet with more
    // than three corners (along the chord between its two neighbouring
    // corners, when those are not selected vertices), so the surrounding
    // facets stay planar.
    bool face_split{false};

    // A boundary vertex (one with an edge used by one facet) is separated
    // into one vertex per facet, and each copy is collapsed out of its facet
    // instead of joining the facets around the vertex.
    bool boundary_tear{false};
};

// For each selected vertex: joins every facet pair around it when it has
// three or more edges, collapses it when it is left with two edges, and
// deletes it when it is left without edges.
void dissolve_vertices(
    const Geometry&               source,
    Geometry&                     destination,
    const std::set<GEO::index_t>& selected_vertices,
    Dissolve_vertices_options     options = {},
    Component_remap*              remap   = nullptr
);

class Dissolve_limited_options
{
public:
    float angle_limit_degrees{5.0f};

    // Also collapse two-valent vertices whose edges are delimited (see the
    // delimit flags); the edge pass never joins across a delimited edge.
    bool  dissolve_boundaries{false};

    // Delimit: an edge whose two facets traverse it in the same direction
    // (winding flip) is never joined across.
    bool  delimit_winding{true};

    // Delimit: an edge with a positive sharpness is never joined across.
    bool  delimit_crease{false};

    // The material delimit of doc/plans/mesh_modeling.md section 4.3 needs
    // per-facet materials, which Geometry does not carry; it is not offered.
};

// Limited dissolve: a heap of manifold edges keyed by the angle between their
// facet normals joins pairs below the angle limit, re-scoring the joined
// facet's edges; then a heap of two-valent vertices keyed by the angle
// between their two edges (as in Dissolve_edges_options) collapses those
// below the limit whose collapse keeps every adjacent corner convex: no
// adjacent corner flips its winding, no other vertex of the facet lies
// inside the triangle the vertex spans with its two neighbours, and no facet
// drops below three corners. With a selection, the candidate edges are the
// selected edges and the edges of selected facets, the candidate vertices
// the selected vertices and the endpoints of candidate edges; with
// selection == nullptr the whole mesh is a candidate.
void dissolve_limited(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection* selection,
    Dissolve_limited_options            options = {},
    Component_remap*                    remap   = nullptr
);

} // namespace erhe::geometry::operation
