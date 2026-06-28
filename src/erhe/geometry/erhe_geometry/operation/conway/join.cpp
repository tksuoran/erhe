#include "erhe_geometry/operation/conway/join.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::geometry::operation {

class Join : public Geometry_operation
{
public:
    Join(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets);

    void build();

private:
    // Direction of the directed edge (va -> vb) within a facet:
    //   +1 the facet traverses va then vb (forward),
    //   -1 the facet traverses vb then va (backward),
    //    0 the edge is not part of the facet.
    [[nodiscard]] auto edge_direction_in_facet(GEO::index_t facet, GEO::index_t va, GEO::index_t vb) const -> int;
};

Join::Join(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets)
    : Geometry_operation{source, destination}
{
    m_selected_facets = selected_facets;
}

auto Join::edge_direction_in_facet(const GEO::index_t facet, const GEO::index_t va, const GEO::index_t vb) const -> int
{
    const GEO::index_t corner_count = source_mesh.facets.nb_corners(facet);
    for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
        const GEO::index_t corner      = source_mesh.facets.corner(facet, local_corner);
        const GEO::index_t next_corner = source_mesh.facets.corner(facet, (local_corner + 1) % corner_count);
        const GEO::index_t v           = source_mesh.facet_corners.vertex(corner);
        const GEO::index_t v_next       = source_mesh.facet_corners.vertex(next_corner);
        if ((v == va) && (v_next == vb)) {
            return 1;
        }
        if ((v == vb) && (v_next == va)) {
            return -1;
        }
    }
    return 0;
}

void Join::build()
{
    // Replace each selected facet with a fan of centroid kites. Every edge interior
    // to the selection (both adjacent facets selected) becomes a quad spanning the
    // two facet centroids and the edge endpoints, with the original edge as the
    // diagonal. An edge on the selection boundary (exactly one adjacent facet
    // selected) becomes a triangle on the selected side that keeps the original edge
    // intact, matching the untouched neighbor. Join inserts no edge midpoints and
    // never moves a vertex, so vertices stay pinned at weight 1.0; the unselected
    // facets are copied through unchanged. With no active selection every facet is
    // selected, so every 2-facet edge becomes a straddling quad - reproducing the
    // whole-mesh behavior.

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

    for (const GEO::index_t src_edge : source_mesh.edges) {
        const std::vector<GEO::index_t>& src_facets = source.get_edge_facets(src_edge);
        // Match the whole-mesh operation: only manifold (2-facet) edges produce
        // geometry. A boundary edge of the mesh itself is skipped (leaving the same
        // hole the classic join leaves).
        if (src_facets.size() != 2) {
            continue;
        }
        const GEO::index_t src_facet_l = src_facets[0];
        const GEO::index_t src_facet_r = src_facets[1];
        const bool selected_l = is_facet_selected(src_facet_l);
        const bool selected_r = is_facet_selected(src_facet_r);
        if (!selected_l && !selected_r) {
            continue; // Edge interior to the unselected region: handled by the copy.
        }

        const GEO::index_t src_vertex_a = source_mesh.edges.vertex(src_edge, 0);
        const GEO::index_t src_vertex_b = source_mesh.edges.vertex(src_edge, 1);
        const GEO::index_t dst_vertex_a = m_vertex_src_to_dst[src_vertex_a];
        const GEO::index_t dst_vertex_b = m_vertex_src_to_dst[src_vertex_b];

        if (selected_l && selected_r) {
            // Interior-to-selection edge: straddling quad using both centroids. The
            // original edge a-b becomes the quad's diagonal.
            const int dir_l = edge_direction_in_facet(src_facet_l, src_vertex_a, src_vertex_b);
            const int dir_r = edge_direction_in_facet(src_facet_r, src_vertex_a, src_vertex_b);
            ERHE_VERIFY((dir_l != 0) && (dir_r != 0) && (dir_l == -dir_r));
            const GEO::index_t new_dst_facet = destination_mesh.facets.create_quads(1);
            if (dir_l == 1) {
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 0, dst_vertex_a);
                make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 1, src_facet_r );
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 2, dst_vertex_b);
                make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 3, src_facet_l );
            } else {
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 0, dst_vertex_a);
                make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 1, src_facet_l );
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 2, dst_vertex_b);
                make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 3, src_facet_r );
            }
        } else {
            // Selection-boundary edge: triangle on the selected side. The base edge
            // a-b is left intact so the spliced unselected neighbor welds to it.
            const GEO::index_t selected_facet = selected_l ? src_facet_l : src_facet_r;
            const int dir = edge_direction_in_facet(selected_facet, src_vertex_a, src_vertex_b);
            ERHE_VERIFY(dir != 0);
            const GEO::index_t new_dst_facet = destination_mesh.facets.create_triangles(1);
            if (dir == 1) {
                // Edge a->b in the selected facet: this is the centroid-side half on
                // the a, b, centroid winding.
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 0, dst_vertex_a);
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 1, dst_vertex_b);
                make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 2, selected_facet);
            } else {
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 0, dst_vertex_a);
                make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 1, selected_facet);
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 2, dst_vertex_b);
            }
        }
    }

    // Re-emit the unselected facets. Join inserts no edge midpoints, so this copies
    // each one 1:1 (no splicing); it is still required so the unselected region
    // reaches the destination. No-op when there is no active selection.
    emit_unselected_facets_with_boundary_splice();

#if !defined(NDEBUG)
    // Join never smooths a vertex: every destination vertex carried over from a
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

void join(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets, Component_remap* remap)
{
    Join operation{source, destination, selected_facets};
    operation.build();
    if ((remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr)) {
        operation.remap_component_selection(*remap->source, *remap->destination);
    }
}

} // namespace erhe::geometry::operation
