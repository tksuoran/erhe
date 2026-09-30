#include "erhe_geometry/operation/edit_mesh_operation.hpp"
#include "erhe_geometry/operation/operation_timing.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <geogram/mesh/mesh.h>

namespace erhe::geometry::operation {

Edit_mesh_operation::Edit_mesh_operation(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
    m_edit_mesh.load(source);
}

void Edit_mesh_operation::emit()
{
    const GEO::index_t vertex_slot_count = m_edit_mesh.get_vertex_slot_count();
    const GEO::index_t facet_slot_count  = m_edit_mesh.get_facet_slot_count();
    const GEO::index_t edge_slot_count   = m_edit_mesh.get_edge_slot_count();

    // Vertices: one batch create (per-element creation is quadratic, see
    // the map_dst_* comment in geometry_operation.hpp).
    m_scratch_to_dst_vertex.assign(vertex_slot_count, GEO::NO_INDEX);
    const GEO::index_t live_vertex_count = m_edit_mesh.get_vertex_count();
    const GEO::index_t first_dst_vertex  = (live_vertex_count > 0) ? destination_mesh.vertices.create_vertices(live_vertex_count) : 0;
    GEO::index_t next_dst_vertex = first_dst_vertex;
    for (GEO::index_t vertex = 0; vertex < vertex_slot_count; ++vertex) {
        if (!m_edit_mesh.is_vertex_alive(vertex)) {
            continue;
        }
        const GEO::index_t dst_vertex = next_dst_vertex++;
        m_scratch_to_dst_vertex[vertex] = dst_vertex;
        const std::vector<Edit_source>& sources = m_edit_mesh.get_vertex(vertex).sources;
        if ((sources.size() == 1) && (sources.front().first == 1.0f)) {
            map_dst_vertex_from_src_vertex(dst_vertex, 1.0f, sources.front().second);
        } else {
            for (const Edit_source& source_entry : sources) {
                add_vertex_source(dst_vertex, source_entry.first, source_entry.second);
            }
        }
    }

    // Facets and corners.
    for (GEO::index_t facet = 0; facet < facet_slot_count; ++facet) {
        if (!m_edit_mesh.is_facet_alive(facet)) {
            continue;
        }
        const Edit_facet&  edit_facet   = m_edit_mesh.get_facet(facet);
        const GEO::index_t corner_count = static_cast<GEO::index_t>(edit_facet.corners.size());
        const GEO::index_t dst_facet    = destination_mesh.facets.create_polygon(corner_count);
        if (edit_facet.source_facet != GEO::NO_INDEX) {
            map_dst_facet_from_src_facet(dst_facet, edit_facet.source_facet);
        }
        for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
            const Edit_corner& edit_corner = edit_facet.corners[local_corner];
            destination_mesh.facets.set_vertex(dst_facet, local_corner, m_scratch_to_dst_vertex[edit_corner.vertex]);
            const GEO::index_t dst_corner = destination_mesh.facets.corner(dst_facet, local_corner);
            for (const Edit_source& source_entry : edit_corner.sources) {
                add_corner_source(dst_corner, source_entry.first, source_entry.second);
            }
        }
    }

    const uint64_t process_flags = structural_post_process_flags;
    {
        Scoped_phase_timer phase_timer{"interpolate"};
        interpolate_mesh_attributes(process_flags);
    }

    // The scratch positions are the result; interpolation only reproduces
    // them for vertices no primitive moved.
    for (GEO::index_t vertex = 0; vertex < vertex_slot_count; ++vertex) {
        const GEO::index_t dst_vertex = m_scratch_to_dst_vertex[vertex];
        if (dst_vertex != GEO::NO_INDEX) {
            set_pointf(destination_mesh.vertices, dst_vertex, m_edit_mesh.get_position(vertex));
        }
    }

    {
        Scoped_phase_timer phase_timer{"sanitize"};
        const std::vector<std::string> warnings = destination.sanitize();
        for (const std::string& warning : warnings) {
            log_operation->error("Edit_mesh_operation::emit(): sanitize: {}", warning);
        }
    }

    {
        Scoped_phase_timer phase_timer{"process"};
        destination.process({.flags = process_flags});
    }

    for (GEO::index_t edge = 0; edge < edge_slot_count; ++edge) {
        if (!m_edit_mesh.is_edge_alive(edge)) {
            continue;
        }
        const Edit_edge& edit_edge = m_edit_mesh.get_edge(edge);
        if (!edit_edge.sharpness.has_value() || edit_edge.facets.empty()) {
            continue;
        }
        destination.set_edge_sharpness(
            m_scratch_to_dst_vertex[edit_edge.vertices[0]],
            m_scratch_to_dst_vertex[edit_edge.vertices[1]],
            edit_edge.sharpness.value()
        );
    }
}

} // namespace erhe::geometry::operation
