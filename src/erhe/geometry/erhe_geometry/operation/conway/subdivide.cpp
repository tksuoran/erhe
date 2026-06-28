#include "erhe_geometry/operation/conway/subdivide.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::geometry::operation {

class Subdivide : public Geometry_operation
{
public:
    Subdivide(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets);

    void build();
};

Subdivide::Subdivide(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets)
    : Geometry_operation{source, destination}
{
    m_selected_facets = selected_facets;
}

void Subdivide::build()
{
    // Add midpoints to edges and connect to the facet centroid. For each corner in
    // a selected src facet, add one quad
    // (previous edge midpoint, corner, next edge midpoint, facet centroid).
    //
    // Subdivide never moves vertices or edge midpoints off the original geometry
    // (vertices stay put with weight 1.0, every edge midpoint is the plain 0.5
    // midpoint on the original segment), so unlike Catmull-Clark there is no
    // boundary smoothing to suppress. An active selection therefore only restricts
    // which edges get a midpoint, which facets get a centroid, and which facets are
    // subdivided; the unselected facets bordering the selection are re-emitted as
    // spliced n-gons so the seam stays watertight. With no active selection every
    // edge / facet qualifies, reproducing the whole-mesh behavior.

    // Every source vertex maps 1:1 to a destination vertex, pinned to itself.
    make_dst_vertices_from_src_vertices();

    // Edge midpoints: one plain 0.5 midpoint per edge incident to a selected facet.
    // The midpoint's vertex/corner sources come from the endpoints (and their
    // corners) of every adjacent facet, so both sides of an interface edge reference
    // the same destination vertex with consistent attributes.
    {
        ERHE_VERIFY(m_src_edge_to_dst_vertex.empty());
        for (const GEO::index_t src_edge : source_mesh.edges) {
            const std::vector<GEO::index_t>& src_edge_facets = source.get_edge_facets(src_edge);
            bool any_selected = false;
            for (const GEO::index_t src_facet : src_edge_facets) {
                if (is_facet_selected(src_facet)) {
                    any_selected = true;
                    break;
                }
            }
            if (!any_selected) {
                continue; // Edge not touched by the selection: no midpoint.
            }
            const GEO::index_t src_vertex_a = source_mesh.edges.vertex(src_edge, 0);
            const GEO::index_t src_vertex_b = source_mesh.edges.vertex(src_edge, 1);
            const std::pair<GEO::index_t, GEO::index_t> src_edge_key = std::make_pair(src_vertex_a, src_vertex_b);
            const GEO::index_t new_dst_vertex = destination_mesh.vertices.create_vertices(1);
            m_src_edge_to_dst_vertex[src_edge_key].push_back(new_dst_vertex);
            for (const GEO::index_t src_facet : src_edge_facets) {
                const GEO::index_t local_a  = source_mesh.facets.find_vertex(src_facet, src_vertex_a);
                const GEO::index_t local_b  = source_mesh.facets.find_vertex(src_facet, src_vertex_b);
                const GEO::index_t corner_a = source_mesh.facets.corner(src_facet, local_a);
                const GEO::index_t corner_b = source_mesh.facets.corner(src_facet, local_b);
                add_vertex_source       (new_dst_vertex, 0.5f, src_vertex_a);
                add_vertex_source       (new_dst_vertex, 0.5f, src_vertex_b);
                add_vertex_corner_source(new_dst_vertex, 0.5f, corner_a);
                add_vertex_corner_source(new_dst_vertex, 0.5f, corner_b);
            }
        }
    }

    // Facet centroids: only for selected facets. Pre-size the lookup so
    // make_new_dst_corner_from_src_facet_centroid's bound assert holds even when the
    // highest-indexed facet is unselected.
    {
        m_src_facet_centroid_to_dst_vertex.resize(source_mesh.facets.nb());
        for (const GEO::index_t src_facet : source_mesh.facets) {
            if (!is_facet_selected(src_facet)) {
                continue;
            }
            make_new_dst_vertex_from_src_facet_centroid(src_facet);
        }
    }

    // Subdivide each selected facet into quads (prev edge midpoint, corner, next
    // edge midpoint, centroid). All edges of a selected facet carry a midpoint, so
    // the NO_VERTEX guard never fires here; it is kept for safety.
    for (const GEO::index_t src_facet : source_mesh.facets) {
        if (!is_facet_selected(src_facet)) {
            continue;
        }
        const GEO::index_t src_corner_count = source_mesh.facets.nb_corners(src_facet);
        if (src_corner_count < 3) {
            continue;
        }
        for (GEO::index_t local_src_facet_corner = 0; local_src_facet_corner < src_corner_count; ++local_src_facet_corner) {
            const GEO::index_t prev_src_corner        = source_mesh.facets.corner(src_facet, (local_src_facet_corner + src_corner_count - 1) % src_corner_count);
            const GEO::index_t src_corner             = source_mesh.facets.corner(src_facet, local_src_facet_corner);
            const GEO::index_t next_src_corner        = source_mesh.facets.corner(src_facet, (local_src_facet_corner + 1) % src_corner_count);
            const GEO::index_t a                      = source_mesh.facet_corners.vertex(prev_src_corner);
            const GEO::index_t b                      = source_mesh.facet_corners.vertex(src_corner     );
            const GEO::index_t c                      = source_mesh.facet_corners.vertex(next_src_corner);
            const GEO::index_t previous_edge_midpoint = get_src_edge_new_vertex(a, b, 0);
            const GEO::index_t next_edge_midpoint     = get_src_edge_new_vertex(b, c, 0);
            if (previous_edge_midpoint == GEO::NO_VERTEX) {
                continue;
            }
            if (next_edge_midpoint == GEO::NO_VERTEX) {
                continue;
            }
            const GEO::index_t new_dst_facet = make_new_dst_facet_from_src_facet(src_facet, 4);
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 0, previous_edge_midpoint);
            make_new_dst_corner_from_src_corner        (new_dst_facet, 1, src_corner);
            make_new_dst_corner_from_dst_vertex        (new_dst_facet, 2, next_edge_midpoint);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 3, src_facet);
        }
    }

    // Re-emit the unselected facets, splicing interface-edge midpoints into the
    // ones that border the selection so the seam is welded (no T-junctions).
    emit_unselected_facets_with_boundary_splice();

#if !defined(NDEBUG)
    // Subdivide never smooths a vertex: every destination vertex carried over from a
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

void subdivide(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets, Component_remap* remap)
{
    Subdivide operation{source, destination, selected_facets};
    operation.build();
    if ((remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr)) {
        operation.remap_component_selection(*remap->source, *remap->destination);
    }
}

} // namespace erhe::geometry::operation
