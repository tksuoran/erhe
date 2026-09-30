#pragma once

#include "erhe_geometry/edit_mesh.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

#include <vector>

namespace erhe::geometry::operation {

// A Geometry_operation whose result is composed on an Edit_mesh scratch.
// The constructor loads m_edit_mesh from the source (which must have edge
// connectivity). A subclass's build() runs Edit_mesh primitives on
// m_edit_mesh, then calls emit(), which writes the scratch into the
// destination through the provenance tables, so every attribute is
// interpolated from the source and remap_component_selection() maps a
// source vertex to the destination vertex whose provenance is that vertex
// with weight 1, and a source facet to the destination facets carrying it.
class Edit_mesh_operation : public Geometry_operation
{
public:
    Edit_mesh_operation(const Geometry& source, Geometry& destination);

protected:
    // Compacts the scratch (tombstoned elements dropped) into the
    // destination: vertices in one create_vertices(n), then one polygon per
    // live facet, provenance recorded for every vertex, corner and facet;
    // attribute interpolation, then the scratch positions (the authority for
    // positions), sanitize() and process() with structural_post_process_flags;
    // finally edge sharpness for every scratch edge that carries it. A wire
    // edge (no facets) has no destination edge; a loose vertex is emitted.
    void emit();

    Edit_mesh m_edit_mesh;

private:
    std::vector<GEO::index_t> m_scratch_to_dst_vertex;
};

} // namespace erhe::geometry::operation
