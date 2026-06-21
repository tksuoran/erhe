#pragma once

#include "tools/mesh_component_selection.hpp" // Mesh_component_mode, Mesh_edge_key

#include <geogram/basic/numeric.h>
#include <geogram/basic/geometry.h> // GEO::vec3f

#include <memory>
#include <set>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace editor {

// How the extruded vertices are subsequently moved by the gizmo drag:
//   none   - by the raw gizmo delta (move_directions left empty);
//   group  - each disjoint (connected) selection subset along its own average normal;
//   vertex - each vertex along its own original vertex normal.
enum class Extrude_normal_mode
{
    none,
    group,
    vertex
};

// Result of building an extruded copy of a Geometry from a mesh component selection.
class Extrude_result
{
public:
    std::shared_ptr<erhe::geometry::Geometry> geometry;           // copy of source + extrude topology
    std::vector<GEO::index_t>                 moved_vertices;      // vertices the gizmo moves (duplicates + interior)
    std::vector<GEO::vec3f>                   move_directions;     // normal modes only: per moved vertex, the unit
                                                                   // (geometry-local) direction it slides along - its
                                                                   // disjoint subset's average normal (group mode) or
                                                                   // its own original vertex normal (vertex mode);
                                                                   // parallel to moved_vertices, empty in none mode
    std::set<GEO::index_t>                    selection_vertices;  // selection sets to carry onto the new geometry
    std::set<Mesh_edge_key>                   selection_edges;
    std::set<GEO::index_t>                    selection_facets;

    [[nodiscard]] auto is_valid() const -> bool
    {
        return static_cast<bool>(geometry) && !moved_vertices.empty();
    }
};

// Build a new Geometry that is `source` plus an extrusion of the selected components.
//   - face   : region extrude of `selected_facets` - the vertices on the selection
//              boundary are duplicated, the selected facets are re-pointed to the
//              duplicates, and bridge quads are added along the boundary. Interior
//              vertices (surrounded only by selected facets) move directly.
//   - edge   : each selected edge's endpoints are duplicated and a bridge quad is added.
//   - vertex : each selected vertex is duplicated and a 2-vertex polygon (an implicit
//              edge) is added.
// The duplicated / moved vertices start coincident with their originals; the caller
// moves them with the gizmo. Original vertex / facet indices are preserved, so the
// caller's `selected_facets` stay valid on the result. Returns an invalid result
// (is_valid() == false) when there is nothing to extrude. The result geometry has its
// connectivity / edges / centroids rebuilt; normals are deferred to
// finalize_extrude_normals() once the move is final (positions are coincident here, so
// the new faces are degenerate).
//
// When `normal_mode` is `group`, the selection is additionally partitioned into disjoint
// connected subsets (face: facets joined across shared selected edges; edge: edges
// joined at shared vertices; vertex: vertices joined by a mesh edge) and each subset's
// unit average normal is computed; `result.move_directions` is then filled parallel to
// `moved_vertices` so the caller can slide each subset along its own normal instead of
// applying the raw gizmo delta. When `vertex`, each moved vertex instead gets the unit
// normal of its own original vertex (the stored vertex normal, else the facet-averaged
// normal). When `none`, `move_directions` is left empty.
[[nodiscard]] auto extrude_mesh_components(
    const erhe::geometry::Geometry& source,
    Mesh_component_mode             mode,
    const std::set<GEO::index_t>&   selected_vertices,
    const std::set<Mesh_edge_key>&  selected_edges,
    const std::set<GEO::index_t>&   selected_facets,
    Extrude_normal_mode             normal_mode
) -> Extrude_result;

// Recompute facet / smooth vertex normals (and collapse them into the stored
// vertex_normal / corner_normal attributes, like Move_mesh_vertices_operation does)
// for an extruded geometry after the move is final. Degenerate facets (e.g. the
// vertex-extrude 2-gons, or zero-length walls) are skipped so no NaN normals leak in.
void finalize_extrude_normals(erhe::geometry::Geometry& geometry);

}
