#pragma once

#include <geogram/basic/geometry.h>
#include <geogram/basic/numeric.h>

#include <set>
#include <utility>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;
class Geometry_component_selection;

// Split, rip and separate of doc/plans/mesh_modeling.md catalog row M9,
// composed on an Edit_mesh scratch (erhe_geometry/edit_mesh.hpp). Blender's
// split (Y), rip (V) and separate (P) are the behaviour reference. Positions
// never change: the result looks like the source until something moves. Every
// duplicated vertex copies its vertex (position and provenance), so vertex and
// corner attributes carry over, and a duplicated edge keeps its sharpness.

// The facets a split or separate acts on for a selection of any mode: the
// selected facets, every facet whose vertices are all selected and every facet
// whose edges are all selected. out_facets is cleared first.
void get_selection_facets(
    const Geometry&                     source,
    const Geometry_component_selection& selection,
    std::set<GEO::index_t>&             out_facets
);

// Region split: the facets of get_selection_facets() are disconnected from the
// rest of the mesh. Each vertex used both by a region facet and by a facet
// outside the region is duplicated; the region facets move onto the copy, the
// other facets keep the original. The remap (both pointers set) selects the
// region: its facets, every vertex of a region facet, every edge of a region
// facet - each set only when the source selection has that set non-empty.
void split_facets(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    Component_remap*                    remap = nullptr
);

// Edge split: tears the mesh along the selected edges (source vertex pairs),
// as rip_vertices() does for an edge selection without a direction: every
// endpoint of a selected edge is separated with the selected edges as the cut.
// The remap selects the torn side's copies of the selected edges.
void split_edges(
    const Geometry&                                        source,
    Geometry&                                              destination,
    const std::set<std::pair<GEO::index_t, GEO::index_t>>& selected_edges,
    Component_remap*                                       remap = nullptr
);

class Rip_options
{
public:
    // Mesh-local direction toward the side that is ripped off (Blender: toward
    // the cursor). Zero picks a deterministic side (see rip_vertices()).
    GEO::vec3f direction{0.0f, 0.0f, 0.0f};
};

// Rip. The torn vertices are the selection's vertices, or the endpoints of its
// edges when it has no vertices. The cut is the selection's edges together with
// every edge between two selected vertices. Each torn vertex is separated into
// one vertex per fan (Edit_mesh::separate_vertex() rule: two facets around the
// vertex are in one fan when they share an edge at it that is not cut), where
// the cut at the vertex is:
// - its cut edges, when they split its facets into two or more fans;
// - otherwise its cut edges plus, one at a time until the facets split, the
//   uncut manifold edge (two or more facets) at the vertex that points most
//   nearly along the preferred direction: at a chain end (one cut edge), the
//   continuation opposite that edge, so the tear runs through the vertex; at a
//   vertex without cut edges, Rip_options::direction (zero: toward the centroid
//   of the vertex's first facet).
// The ripped side: each connected run of cut edges is oriented by a walk from
// its lowest end vertex, and the facet traversing each cut edge in the walk
// direction is on the ripped side; with a non-zero direction the side whose
// facets lie toward the direction is ripped instead. At a vertex without cut
// edges the fan holding the facet whose centroid lies most nearly along the
// direction is ripped. Each ripped fan gets a new vertex; of the other fans the
// first keeps the vertex and the rest get new vertices. The remap selects the
// ripped vertices (when the source selection has vertices) and the ripped
// copies of the cut edges (when it has edges). A torn vertex that does not
// separate (one facet, or no manifold edge to cut) stays as it is and stays
// selected.
void rip_vertices(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    Rip_options                         options = {},
    Component_remap*                    remap   = nullptr
);

// Separate: two results from one source. destination_kept is the source
// without the selected facets (Delete_context::faces: their edges and vertices
// that no other facet uses go too); destination_extracted holds only the
// selected facets and their vertices. Both keep every attribute. The remaps
// carry their source selection to each result by provenance (the kept remap
// loses the selected facets, the extracted remap maps them onto the extracted
// facets).
void extract_facets(
    const Geometry&               source,
    Geometry&                     destination_kept,
    Geometry&                     destination_extracted,
    const std::set<GEO::index_t>& selected_facets,
    Component_remap*              kept_remap      = nullptr,
    Component_remap*              extracted_remap = nullptr
);

} // namespace erhe::geometry::operation
