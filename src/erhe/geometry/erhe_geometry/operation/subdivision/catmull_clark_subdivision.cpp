#include "erhe_geometry/operation/subdivision/catmull_clark_subdivision.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::geometry::operation {

class Catmull_clark_subdivision : public Geometry_operation
{
public:
    Catmull_clark_subdivision(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets);
    void build();
};

// E = average of two neighboring facet vertices and original endpoints
//
// Compute P'             F = average F of all n facet vertices for facets touching P
//                        R = average R of all n edge midpoints for edges touching P
//      F + 2R + (n-3)P   P = old point location
// P' = ----------------
//            n           -> F weight is     1/n
//                        -> R weight is     2/n
//      F   2R   (n-3)P   -> P weight is (n-3)/n
// P' = - + -- + ------
//      n    n      n
//
// For each corner in the src facet, add one quad
// (centroid, previous edge 'edge midpoint', corner, next edge 'edge midpoint')
Catmull_clark_subdivision::Catmull_clark_subdivision(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets)
    : Geometry_operation{source, destination}
{
    m_selected_facets = selected_facets;
}

void Catmull_clark_subdivision::build()
{
    const GEO::index_t vertex_count = source_mesh.vertices.nb();

    // Per-vertex classification driving the boundary-pinning rules. An
    // interior-selected vertex (all incident facets selected) gets the full
    // Catmull-Clark smoothing; every other vertex (a selection-boundary vertex, a
    // fully-unselected vertex, or an isolated/low-valence vertex) is pinned to its
    // original position so the modified region stays welded to the rest. With no
    // active selection every vertex is interior-selected, reproducing the classic
    // whole-mesh behavior.
    std::vector<uint8_t> vertex_touches_selected  (vertex_count, 0);
    std::vector<uint8_t> vertex_touches_unselected(vertex_count, 0);
    for (GEO::index_t src_facet : source_mesh.facets) {
        const bool selected = is_facet_selected(src_facet);
        for (GEO::index_t src_corner : source_mesh.facets.corners(src_facet)) {
            const GEO::index_t src_vertex = source_mesh.facet_corners.vertex(src_corner);
            if (selected) {
                vertex_touches_selected[src_vertex] = 1;
            } else {
                vertex_touches_unselected[src_vertex] = 1;
            }
        }
    }
    const auto interior_selected = [&](const GEO::index_t vertex) -> bool {
        return (vertex_touches_selected[vertex] != 0) && (vertex_touches_unselected[vertex] == 0);
    };

    //                       (n-3)P
    // Make initial P's with ------  (interior-selected vertices only; everything
    //                          n     else is pinned with weight 1.0 to itself).
    {
        for (GEO::index_t vertex : source_mesh.vertices) {
            const std::vector<GEO::index_t>& corners = source.get_vertex_corners(vertex);
            if (interior_selected(vertex) && (corners.size() >= 3)) {
                const float n = static_cast<float>(corners.size());
                // n = 0   -> centroid points, safe to skip
                // n = 1,2 -> ?
                // n = 3   -> ?
                const float weight = (n - 3.0f) / n;
                make_new_dst_vertex_from_src_vertex(weight, vertex);
            } else {
                make_new_dst_vertex_from_src_vertex(1.0f, vertex);
            }
        }
    }

    // Make new edge midpoints for every edge incident to at least one selected
    // facet. An interior edge (no unselected adjacent facet) uses the full
    // Catmull-Clark edge point (endpoints + adjacent facet centroids). An interface
    // edge (also borders an unselected facet) uses the plain midpoint so it lands
    // exactly on the original segment, keeping the spliced unselected n-gon
    // watertight; corner sources for it come from the endpoints (both sides).
    //
    // Add midpoint (R) to each new endpoint, but only for interior-selected
    // endpoints (pinned endpoints must not move).
    //   R = average R of all n edge midpoints for edges touching P
    //  2R  we add both edge end points with weight 1 so total edge weight is 2
    //  --
    //   n
    {
        ERHE_VERIFY(m_src_edge_to_dst_vertex.empty());
        for (GEO::index_t src_edge : source_mesh.edges) {
            const std::vector<GEO::index_t>& src_edge_facets = source.get_edge_facets(src_edge);
            GEO::index_t selected_facet_count   = 0;
            GEO::index_t unselected_facet_count = 0;
            for (GEO::index_t src_facet : src_edge_facets) {
                if (is_facet_selected(src_facet)) {
                    ++selected_facet_count;
                } else {
                    ++unselected_facet_count;
                }
            }
            if (selected_facet_count == 0) {
                continue; // Edge not touched by the selection: no midpoint.
            }
            const bool interior_edge = (unselected_facet_count == 0);

            const GEO::index_t src_vertex_a = source_mesh.edges.vertex(src_edge, 0);
            const GEO::index_t src_vertex_b = source_mesh.edges.vertex(src_edge, 1);
            const std::pair<GEO::index_t, GEO::index_t> src_edge_key = std::make_pair(src_vertex_a, src_vertex_b);
            const GEO::index_t new_dst_vertex = destination_mesh.vertices.create_vertices(1);
            m_src_edge_to_dst_vertex[src_edge_key].push_back(new_dst_vertex);
            add_vertex_source(new_dst_vertex, 1.0f, src_vertex_a);
            add_vertex_source(new_dst_vertex, 1.0f, src_vertex_b);

            if (interior_edge) {
                // Full Catmull-Clark edge point: also pull in the adjacent facet
                // centroids (this is what moves the midpoint off the segment). The
                // facet-centroid contribution also supplies the midpoint's corner
                // sources.
                for (GEO::index_t src_facet : src_edge_facets) {
                    const GEO::index_t src_facet_corner_count = source_mesh.facets.nb_corners(src_facet);
                    if (src_facet_corner_count == 0) {
                        continue;
                    }
                    const float weight = 1.0f / static_cast<float>(src_facet_corner_count);
                    add_facet_centroid(new_dst_vertex, weight, src_facet);
                }
            } else {
                // Interface edge: plain midpoint on the segment. Supply corner
                // sources from the endpoints of every adjacent facet so the spliced
                // n-gon corners inherit texcoords / colors.
                for (GEO::index_t src_facet : src_edge_facets) {
                    const GEO::index_t local_a  = source_mesh.facets.find_vertex(src_facet, src_vertex_a);
                    const GEO::index_t local_b  = source_mesh.facets.find_vertex(src_facet, src_vertex_b);
                    const GEO::index_t corner_a = source_mesh.facets.corner(src_facet, local_a);
                    const GEO::index_t corner_b = source_mesh.facets.corner(src_facet, local_b);
                    add_vertex_corner_source(new_dst_vertex, 1.0f, corner_a);
                    add_vertex_corner_source(new_dst_vertex, 1.0f, corner_b);
                }
            }

            // R contribution to the interior-selected endpoints only.
            if (interior_selected(src_vertex_a)) {
                const float n_a = static_cast<float>(source.get_vertex_corners(src_vertex_a).size());
                ERHE_VERIFY(n_a != 0.0f);
                const float weight_a = 1.0f / n_a;
                const GEO::index_t new_dst_vertex_a = m_vertex_src_to_dst[src_vertex_a];
                add_vertex_source(new_dst_vertex_a, weight_a, src_vertex_a);
                add_vertex_source(new_dst_vertex_a, weight_a, src_vertex_b);
            }
            if (interior_selected(src_vertex_b)) {
                const float n_b = static_cast<float>(source.get_vertex_corners(src_vertex_b).size());
                ERHE_VERIFY(n_b != 0.0f);
                const float weight_b = 1.0f / n_b;
                const GEO::index_t new_dst_vertex_b = m_vertex_src_to_dst[src_vertex_b];
                add_vertex_source(new_dst_vertex_b, weight_b, src_vertex_a);
                add_vertex_source(new_dst_vertex_b, weight_b, src_vertex_b);
            }
        }
    }

    // Facet centroids: only for selected facets. Pre-size the lookup so
    // make_new_dst_corner_from_src_facet_centroid's bound assert holds even when the
    // highest-indexed facet is unselected.
    {
        m_src_facet_centroid_to_dst_vertex.resize(source_mesh.facets.nb());
        for (GEO::index_t src_facet : source_mesh.facets) {
            if (!is_facet_selected(src_facet)) {
                continue;
            }
            make_new_dst_vertex_from_src_facet_centroid(src_facet);

            // Add facet centroids (F) to interior-selected corner vertices only.
            // F = average F of all n facet vertices for faces touching P
            //  F    <- because F is average of all centroids, it adds extra /n
            // ---
            //  n
            const GEO::index_t nb_vertices = source_mesh.facets.nb_vertices(src_facet);
            if (nb_vertices == 0) {
                continue;
            }
            const auto corner_weight = 1.0f / static_cast<float>(nb_vertices);
            for (GEO::index_t src_corner : source_mesh.facets.corners(src_facet)) {
                const GEO::index_t src_vertex = source_mesh.facet_corners.vertex(src_corner);
                if (!interior_selected(src_vertex)) {
                    continue;
                }
                const GEO::index_t               dst_vertex         = m_vertex_src_to_dst[src_vertex];
                const std::vector<GEO::index_t>& src_vertex_corners = source.get_vertex_corners(src_vertex);
                if (src_vertex_corners.empty()) {
                    continue;
                }
                const float vertex_weight = 1.0f / static_cast<float>(src_vertex_corners.size());
                add_facet_centroid(dst_vertex, vertex_weight * vertex_weight * corner_weight, src_facet);
            }
        }
    }

    // Subdivide the selected facets into quads (centroid, prev edge midpoint,
    // corner, next edge midpoint). All edges of a selected facet carry a midpoint,
    // so the NO_VERTEX guard never fires here; it is kept for safety.
    {
        for (GEO::index_t src_facet : source_mesh.facets) {
            if (!is_facet_selected(src_facet)) {
                continue;
            }
            const GEO::index_t corner_count = source_mesh.facets.nb_vertices(src_facet);
            if (corner_count < 3) {
                continue;
            }
            for (GEO::index_t local_facet_corner = 0; local_facet_corner < corner_count; ++local_facet_corner) {
                const GEO::index_t prev_local_facet_corner = (local_facet_corner + corner_count - 1) % corner_count;
                const GEO::index_t next_local_facet_corner = (local_facet_corner +                1) % corner_count;
                const GEO::index_t prev_src_corner         = source_mesh.facets.corner(src_facet, prev_local_facet_corner);
                const GEO::index_t src_corner              = source_mesh.facets.corner(src_facet, local_facet_corner);
                const GEO::index_t next_corner             = source_mesh.facets.corner(src_facet, next_local_facet_corner);
                const GEO::index_t prev_src_vertex         = source_mesh.facet_corners.vertex(prev_src_corner);
                const GEO::index_t src_vertex              = source_mesh.facet_corners.vertex(src_corner);
                const GEO::index_t next_src_vertex         = source_mesh.facet_corners.vertex(next_corner);
                const GEO::index_t previous_edge_midpoint  = get_src_edge_new_vertex(prev_src_vertex, src_vertex, 0);
                const GEO::index_t next_edge_midpoint      = get_src_edge_new_vertex(src_vertex, next_src_vertex, 0);
                if (previous_edge_midpoint == GEO::NO_VERTEX || next_edge_midpoint == GEO::NO_VERTEX) {
                    continue;
                }
                const GEO::index_t new_dst_facet           = make_new_dst_facet_from_src_facet(src_facet, 4);
                make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 0, src_facet);
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 1, previous_edge_midpoint);
                make_new_dst_corner_from_src_corner        (new_dst_facet, 2, src_corner);
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 3, next_edge_midpoint);
            }
        }
    }

    // Re-emit the unselected facets, splicing interface-edge midpoints into the
    // ones that border the selection so the seam is welded (no T-junctions).
    emit_unselected_facets_with_boundary_splice();

#if !defined(NDEBUG)
    // A pinned vertex (boundary, fully-unselected, or isolated) must end with
    // exactly one source (1.0, itself). Anything else means a stray F/R
    // contribution leaked onto it and the boundary would crack. Interior-selected
    // vertices are smoothed (self + R + F) and are not pinned, so skip them.
    for (GEO::index_t vertex = 0; vertex < vertex_count; ++vertex) {
        if (interior_selected(vertex)) {
            continue;
        }
        const GEO::index_t dst_vertex = m_vertex_src_to_dst[vertex];
        const std::vector<std::pair<float, GEO::index_t>>& sources = m_dst_vertex_sources.get(dst_vertex);
        ERHE_VERIFY(sources.size() == 1);
        ERHE_VERIFY(sources[0].first == 1.0f);
        ERHE_VERIFY(sources[0].second == vertex);
    }
#endif

    post_processing();
}

void catmull_clark_subdivision(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets)
{
    Catmull_clark_subdivision operation{source, destination, selected_facets};
    operation.build();
}

} // namespace erhe::geometry::operation
