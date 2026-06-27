#include "erhe_geometry/operation/conway/truncate.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_verify/verify.hpp"

#include <vector>

namespace erhe::geometry::operation {

class Truncate : public Geometry_operation
{
public:
    Truncate(const Geometry& source, Geometry& destination, float ratio, const std::set<GEO::index_t>* selected_facets);

    void build();
    float m_ratio;
};

Truncate::Truncate(const Geometry& source, Geometry& destination, float ratio, const std::set<GEO::index_t>* selected_facets)
    : Geometry_operation{source, destination}
    , m_ratio{ratio}
{
    m_selected_facets = selected_facets;
}

void Truncate::build()
{
    // Split each edge at ratio and 1 - ratio from each endpoint.
    // ratio = 1/3 gives standard truncation, ratio = 0.5 gives ambo.
    const float t0 = m_ratio;
    const float t1 = 1.0f - m_ratio;

    const GEO::index_t vertex_count = source_mesh.vertices.nb();

    // Classify each vertex. A vertex interior to the selection (every incident facet
    // selected) is removed and replaced by a vertex-face, exactly like the whole-mesh
    // truncation. Every other vertex (a selection-boundary vertex or a fully
    // unselected vertex) is KEPT - carried over pinned to weight 1.0 - so the
    // unselected facets and the corner-cap triangles can reference it and the seam
    // stays welded. With no active selection every vertex is interior, reproducing
    // the whole-mesh behavior (all vertices removed).
    std::vector<uint8_t> vertex_touches_selected  (vertex_count, 0);
    std::vector<uint8_t> vertex_touches_unselected(vertex_count, 0);
    for (const GEO::index_t src_facet : source_mesh.facets) {
        const bool selected = is_facet_selected(src_facet);
        for (const GEO::index_t src_corner : source_mesh.facets.corners(src_facet)) {
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

    // Keep (carry over) every non-interior vertex.
    for (const GEO::index_t src_vertex : source_mesh.vertices) {
        if (!interior_selected(src_vertex)) {
            make_new_dst_vertex_from_src_vertex(src_vertex);
        }
    }

    // Two split points (ratio, 1 - ratio) on every edge incident to a selected facet.
    make_selected_edge_midpoints({t0, t1});

    // Vertex-faces: one per interior-to-selection vertex, built from the near-vertex
    // edge midpoints around it (all incident edges are interior, so every midpoint
    // exists). Boundary vertices get corner caps below instead.
    for (const GEO::index_t src_vertex : source_mesh.vertices) {
        if (!interior_selected(src_vertex)) {
            continue;
        }
        const std::vector<GEO::index_t>& src_corners      = source.get_vertex_corners(src_vertex);
        const GEO::index_t               src_corner_count = static_cast<GEO::index_t>(src_corners.size());
        if (src_corner_count < 3) {
            continue;
        }

        std::vector<GEO::index_t> midpoints;
        midpoints.reserve(src_corner_count);
        bool all_valid = true;
        for (GEO::index_t i = 0; i < src_corner_count; ++i) {
            const GEO::index_t src_corner = src_corners[i];
            const GEO::index_t src_facet  = source.get_corner_facet(src_corner);
            if (src_facet >= source_mesh.facets.nb()) {
                all_valid = false;
                break;
            }
            const GEO::index_t next_src_corner = source_mesh.facets.next_corner_around_facet(src_facet, src_corner);
            const GEO::index_t next_src_vertex = source_mesh.facet_corners.vertex(next_src_corner);
            const GEO::index_t edge_midpoint   = get_src_edge_new_vertex(src_vertex, next_src_vertex, 0);
            if (edge_midpoint == GEO::NO_VERTEX) {
                all_valid = false;
                break;
            }
            midpoints.push_back(edge_midpoint);
        }
        if (!all_valid) {
            continue;
        }

        const GEO::index_t new_dst_facet = destination_mesh.facets.create_polygon(src_corner_count);
        for (GEO::index_t i = 0; i < src_corner_count; ++i) {
            make_new_dst_corner_from_dst_vertex(new_dst_facet, i, midpoints[i]);
        }
    }

    // Shrunken faces: one per selected facet, two midpoints per edge.
    for (const GEO::index_t src_facet : source_mesh.facets) {
        if (!is_facet_selected(src_facet)) {
            continue;
        }
        const GEO::index_t src_corner_count = source_mesh.facets.nb_corners(src_facet);
        if (src_corner_count < 3) {
            continue;
        }

        std::vector<std::pair<GEO::index_t, GEO::index_t>> edge_pairs;
        edge_pairs.reserve(src_corner_count);
        bool all_valid = true;
        for (GEO::index_t i = 0; i < src_corner_count; ++i) {
            const GEO::index_t src_corner      = source_mesh.facets.corner(src_facet, i);
            const GEO::index_t next_src_corner = source_mesh.facets.corner(src_facet, (i + 1) % src_corner_count);
            const GEO::index_t src_vertex      = source_mesh.facet_corners.vertex(src_corner);
            const GEO::index_t next_src_vertex = source_mesh.facet_corners.vertex(next_src_corner);
            const GEO::index_t a = get_src_edge_new_vertex(src_vertex, next_src_vertex, 0); // near src_vertex
            const GEO::index_t b = get_src_edge_new_vertex(src_vertex, next_src_vertex, 1); // near next_src_vertex
            if ((a == GEO::NO_VERTEX) || (b == GEO::NO_VERTEX)) {
                all_valid = false;
                break;
            }
            edge_pairs.emplace_back(a, b);
        }
        if (!all_valid) {
            continue;
        }

        const GEO::index_t new_dst_facet = destination_mesh.facets.create_polygon(src_corner_count * 2);
        GEO::index_t dst_corner = 0;
        for (const auto& [a, b] : edge_pairs) {
            make_new_dst_corner_from_dst_vertex(new_dst_facet, dst_corner++, a);
            make_new_dst_corner_from_dst_vertex(new_dst_facet, dst_corner++, b);
        }
    }

    // Corner caps: at every kept (boundary) vertex of a selected facet, the shrunken
    // face leaves the corner cut open along (near-v on the entering edge) -> (near-v
    // on the leaving edge). Fill that notch with a triangle back to the kept vertex.
    // The cap's edges weld to the shrunken face (the reverse of its cut edge) and to
    // the spliced unselected neighbor (the two short edges between v and the near-v
    // midpoints), so the boundary stays watertight.
    for (const GEO::index_t src_facet : source_mesh.facets) {
        if (!is_facet_selected(src_facet)) {
            continue;
        }
        const GEO::index_t src_corner_count = source_mesh.facets.nb_corners(src_facet);
        if (src_corner_count < 3) {
            continue;
        }
        for (GEO::index_t i = 0; i < src_corner_count; ++i) {
            const GEO::index_t src_corner = source_mesh.facets.corner(src_facet, i);
            const GEO::index_t v          = source_mesh.facet_corners.vertex(src_corner);
            if (interior_selected(v)) {
                continue; // Covered by the vertex-face.
            }
            const GEO::index_t prev_src_corner = source_mesh.facets.corner(src_facet, (i + src_corner_count - 1) % src_corner_count);
            const GEO::index_t next_src_corner = source_mesh.facets.corner(src_facet, (i + 1) % src_corner_count);
            const GEO::index_t u               = source_mesh.facet_corners.vertex(prev_src_corner); // entering edge u -> v
            const GEO::index_t w               = source_mesh.facet_corners.vertex(next_src_corner); // leaving  edge v -> w
            const GEO::index_t b_prev          = get_src_edge_new_vertex(u, v, 1); // near v on entering edge
            const GEO::index_t a_next          = get_src_edge_new_vertex(v, w, 0); // near v on leaving  edge
            if ((b_prev == GEO::NO_VERTEX) || (a_next == GEO::NO_VERTEX)) {
                continue;
            }
            const GEO::index_t new_dst_facet = destination_mesh.facets.create_triangles(1);
            make_new_dst_corner_from_dst_vertex(new_dst_facet, 0, a_next);
            make_new_dst_corner_from_dst_vertex(new_dst_facet, 1, b_prev);
            make_new_dst_corner_from_src_corner(new_dst_facet, 2, src_corner);
        }
    }

    // Re-emit the unselected facets, splicing both interface-edge split points into
    // the ones that border the selection so the seam is welded (no T-junctions).
    emit_unselected_facets_with_boundary_splice();

#if !defined(NDEBUG)
    // Every KEPT (non-interior) vertex must end pinned with exactly one source
    // (1.0, itself). Interior vertices were removed (no dst vertex) and are skipped.
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

void truncate(const Geometry& source, Geometry& destination, float ratio, const std::set<GEO::index_t>* selected_facets)
{
    Truncate operation{source, destination, ratio, selected_facets};
    operation.build();
}

} // namespace erhe::geometry::operation
