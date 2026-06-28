#include "erhe_geometry/operation/conway/kis.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::geometry::operation {

class Kis : public Geometry_operation
{
public:
    Kis(const Geometry& source, Geometry& destination, float height, const std::set<GEO::index_t>* selected_facets);

    void build();
    float m_height;
};

Kis::Kis(const Geometry& source, Geometry& destination, float height, const std::set<GEO::index_t>* selected_facets)
    : Geometry_operation{source, destination}
    , m_height{height}
{
    m_selected_facets = selected_facets;
}

void Kis::build()
{
    // Raise a centroid over each selected facet and fan it into triangles
    // (centroid, corner, next corner). Kis keeps every original edge intact (no edge
    // midpoints), so vertices stay pinned at weight 1.0 and the selected fan
    // triangles share their boundary edges with the untouched neighbors. An active
    // selection therefore only restricts which facets get a centroid and are fanned;
    // the unselected facets are copied through (no midpoints to splice in) so the
    // result stays watertight. With no active selection every facet qualifies,
    // reproducing the whole-mesh behavior.

    // Every source vertex maps 1:1 to a destination vertex, pinned to itself.
    make_dst_vertices_from_src_vertices();

    // Facet centroids: only for selected facets. Pre-size the lookup so
    // make_new_dst_corner_from_src_facet_centroid's bound assert holds even when the
    // highest-indexed facet is unselected.
    m_src_facet_centroid_to_dst_vertex.resize(source_mesh.facets.nb());
    for (const GEO::index_t src_facet : source_mesh.facets) {
        if (!is_facet_selected(src_facet)) {
            continue;
        }
        make_new_dst_vertex_from_src_facet_centroid(src_facet);
    }

    // Fan each selected facet from its centroid.
    for (const GEO::index_t src_facet : source_mesh.facets) {
        if (!is_facet_selected(src_facet)) {
            continue;
        }
        const GEO::index_t src_facet_corner_count = source_mesh.facets.nb_corners(src_facet);
        if (src_facet_corner_count < 3) {
            continue;
        }
        for (GEO::index_t local_src_facet_corner = 0; local_src_facet_corner < src_facet_corner_count; ++local_src_facet_corner) {
            const GEO::index_t src_corner      = source_mesh.facets.corner(src_facet, local_src_facet_corner);
            const GEO::index_t next_src_corner = source_mesh.facets.corner(src_facet, (local_src_facet_corner + 1) % src_facet_corner_count);
            const GEO::index_t new_dst_facet   = destination_mesh.facets.create_triangles(1);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 0, src_facet);
            make_new_dst_corner_from_src_corner        (new_dst_facet, 1, src_corner);
            make_new_dst_corner_from_src_corner        (new_dst_facet, 2, next_src_corner);
        }
    }

    // Re-emit the unselected facets. Kis inserts no edge midpoints, so this copies
    // each one 1:1 (no splicing); it is still required so the unselected region
    // reaches the destination. No-op when there is no active selection.
    emit_unselected_facets_with_boundary_splice();

#if !defined(NDEBUG)
    // Kis never smooths a vertex: every destination vertex carried over from a
    // source vertex must end with exactly one source (1.0, itself). Anything else
    // means a stray contribution leaked onto it and the boundary would crack.
    for (GEO::index_t vertex = 0; vertex < source_mesh.vertices.nb(); ++vertex) {
        const GEO::index_t dst_vertex = m_vertex_src_to_dst[vertex];
        const std::vector<std::pair<float, GEO::index_t>>& sources = m_dst_vertex_sources.get(dst_vertex);
        ERHE_VERIFY(sources.size() == 1);
        ERHE_VERIFY(sources[0].first == 1.0f);
        ERHE_VERIFY(sources[0].second == vertex);
    }
#endif

    post_processing();

    // Offset each selected facet's centroid vertex along the face normal by height.
    if (m_height != 0.0f) {
        for (const GEO::index_t src_facet : source_mesh.facets) {
            if (!is_facet_selected(src_facet)) {
                continue;
            }
            const GEO::index_t dst_vertex = m_src_facet_centroid_to_dst_vertex[src_facet];
            if (dst_vertex == GEO::NO_INDEX) {
                continue;
            }
            const GEO::vec3f normal = mesh_facet_normalf(source_mesh, src_facet);
            const GEO::vec3f pos = get_pointf(destination_mesh.vertices, dst_vertex);
            set_pointf(destination_mesh.vertices, dst_vertex, pos + m_height * normal);
        }
    }
}

void kis(const Geometry& source, Geometry& destination, float height, const std::set<GEO::index_t>* selected_facets, Component_remap* remap)
{
    Kis operation{source, destination, height, selected_facets};
    operation.build();
    if ((remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr)) {
        operation.remap_component_selection(*remap->source, *remap->destination);
    }
}

} // namespace erhe::geometry::operation
