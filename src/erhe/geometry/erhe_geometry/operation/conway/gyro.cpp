#include "erhe_geometry/operation/conway/gyro.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::geometry::operation {

class Gyro : public Geometry_operation
{
public:
    Gyro(const Geometry& source, Geometry& destination, float ratio, const std::set<GEO::index_t>* selected_facets);

    void build();
    float m_ratio;
};

Gyro::Gyro(const Geometry& source, Geometry& destination, float ratio, const std::set<GEO::index_t>* selected_facets)
    : Geometry_operation{source, destination}
    , m_ratio{ratio}
{
    m_selected_facets = selected_facets;
}

void Gyro::build()
{
    // For each corner in a selected src facet, create a pentagon from:
    // (prev edge midpoint 0, prev edge midpoint 1, corner vertex,
    //  next edge midpoint 0, face centroid)
    // ratio controls the edge split positions (ratio and 1 - ratio).
    //
    // Gyro keeps both split points on the original edge segment (it never moves a
    // vertex or split point off the geometry), so unlike Catmull-Clark there is no
    // boundary smoothing to suppress. An active selection only restricts which edges
    // are split, which facets get a centroid, and which facets are fanned; the
    // unselected facets bordering the selection are re-emitted as n-gons that splice
    // in BOTH boundary-edge split points so the seam stays watertight. With no active
    // selection every edge / facet qualifies, reproducing the whole-mesh behavior.

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

    // Two split points (ratio, 1 - ratio) on every edge incident to a selected facet.
    make_selected_edge_midpoints({m_ratio, 1.0f - m_ratio});

    // Fan each selected facet into pentagons. All edges of a selected facet carry
    // both split points, so the NO_VERTEX guard never fires here; it is kept for
    // safety.
    for (const GEO::index_t src_facet : source_mesh.facets) {
        if (!is_facet_selected(src_facet)) {
            continue;
        }
        const GEO::index_t src_corner_count = source_mesh.facets.nb_corners(src_facet);
        if (src_corner_count < 3) {
            continue;
        }
        for (GEO::index_t local_src_facet_corner = 0; local_src_facet_corner < src_corner_count; ++local_src_facet_corner) {
            const GEO::index_t prev_src_corner          = source_mesh.facets.corner(src_facet, (local_src_facet_corner + src_corner_count - 1) % src_corner_count);
            const GEO::index_t src_corner               = source_mesh.facets.corner(src_facet, local_src_facet_corner);
            const GEO::index_t next_src_corner          = source_mesh.facets.corner(src_facet, (local_src_facet_corner + 1) % src_corner_count);
            const GEO::index_t a                        = source_mesh.facet_corners.vertex(prev_src_corner);
            const GEO::index_t b                        = source_mesh.facet_corners.vertex(src_corner     );
            const GEO::index_t c                        = source_mesh.facet_corners.vertex(next_src_corner);
            const GEO::index_t previous_edge_midpoint_0 = get_src_edge_new_vertex(a, b, 0);
            const GEO::index_t previous_edge_midpoint_1 = get_src_edge_new_vertex(a, b, 1);
            const GEO::index_t next_edge_midpoint_0     = get_src_edge_new_vertex(b, c, 0);
            if (previous_edge_midpoint_0 == GEO::NO_VERTEX ||
                previous_edge_midpoint_1 == GEO::NO_VERTEX ||
                next_edge_midpoint_0     == GEO::NO_VERTEX) {
                continue;
            }
            const GEO::index_t new_dst_facet = make_new_dst_facet_from_src_facet(src_facet, 5);
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 0, previous_edge_midpoint_0);
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 1, previous_edge_midpoint_1);
            make_new_dst_corner_from_src_corner        (new_dst_facet, 2, src_corner);
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 3, next_edge_midpoint_0);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 4, src_facet);
        }
    }

    // Re-emit the unselected facets, splicing both interface-edge split points into
    // the ones that border the selection so the seam is welded (no T-junctions).
    emit_unselected_facets_with_boundary_splice();

#if !defined(NDEBUG)
    // Gyro never smooths a vertex: every destination vertex carried over from a
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
}

void gyro(const Geometry& source, Geometry& destination, float ratio, const std::set<GEO::index_t>* selected_facets, Component_remap* remap)
{
    Gyro operation{source, destination, ratio, selected_facets};
    operation.build();
    if ((remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr)) {
        operation.remap_component_selection(*remap->source, *remap->destination);
    }
}

} // namespace erhe::geometry::operation
