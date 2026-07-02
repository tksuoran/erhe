#include "erhe_geometry/operation/subdivision/sqrt3_subdivision.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::geometry::operation {

class Sqrt3_subdivision : public Geometry_operation
{
public:
    Sqrt3_subdivision(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets);

    void build(Post_processing post_processing_level);
};

Sqrt3_subdivision::Sqrt3_subdivision(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets)
    : Geometry_operation{source, destination}
{
    m_selected_facets = selected_facets;
}

//  Sqrt(3): Replace edges with two triangles
//  For each corner in the src facet, add one triangle
//  (centroid, corner, opposite centroid)
//
//  Centroids:
//
//  (1) q := 1/3 (p_i + p_j + p_k)
//
//  Refine old vertices:
//
//  (2) S(p) := (1 - alpha_n) p + alpha_n 1/n SUM p_i
//
//  (6) alpha_n = (4 - 2 cos(2Pi/n)) / 9
void Sqrt3_subdivision::build(const Post_processing post_processing_level)
{
    constexpr static float pi = 3.141592653589793238462643383279502884197169399375105820974944592308f;

    // TODO At least assert these are available
    // build_src_vertex_to_src_corners();

    const GEO::index_t vertex_count = source_mesh.vertices.nb();

    // Per-vertex classification driving the boundary-pinning rules. An
    // interior-selected vertex (all incident facets selected) gets the full sqrt3
    // smoothing; every other vertex (a selection-boundary vertex, a fully-unselected
    // vertex, or an isolated one) is pinned to its original position so the modified
    // region stays welded to the rest. With no active selection every vertex is
    // interior-selected, reproducing the classic whole-mesh behavior.
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

    // Refine old vertices: smooth every interior-selected vertex with S(p); pin
    // everything else with weight 1.0 to itself.
    for (const GEO::index_t src_vertex : source_mesh.vertices) {
        const std::vector<GEO::index_t>& src_corners = source.get_vertex_corners(src_vertex);
        if (!interior_selected(src_vertex) || src_corners.empty()) {
            make_new_dst_vertex_from_src_vertex(1.0f, src_vertex);
            continue;
        }
        const float        n                = static_cast<float>(src_corners.size());
        const float        alpha            = (4.0f - 2.0f * std::cos(2.0f * pi / n)) / 9.0f;
        const float        alpha_per_n      = alpha / n;
        const float        alpha_complement = 1.0f - alpha;
        const GEO::index_t new_dst_vertex   = make_new_dst_vertex_from_src_vertex(alpha_complement, src_vertex);
        add_vertex_ring(new_dst_vertex, alpha_per_n, src_vertex);
    }

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

    // Walk every directed edge of every selected facet. Each produces one triangle
    // rooted at this facet's centroid; the third corner depends on the opposite facet:
    //  * interior-to-selection edge (opposite facet selected): FLIP. Third corner is
    //    the opposite centroid, so the original edge a-b becomes the centroid-centroid
    //    diagonal. The matching half-triangle comes from the opposite facet's own
    //    iteration of the same (reversed) edge.
    //  * selection-boundary edge (opposite facet unselected): NO flip. Third corner is
    //    the next corner, so the original edge a-b is kept (a kis-style fan triangle).
    //    The unselected neighbor, copied 1:1 below, welds to that edge.
    //  * mesh-boundary edge (no opposite facet): skipped, exactly like whole-mesh
    //    sqrt3 - the existing hole is left as-is.
    // With no active selection every opposite facet is "selected", so every interior
    // edge flips - reproducing the whole-mesh operation.
    for (const GEO::index_t src_facet : source_mesh.facets) {
        if (!is_facet_selected(src_facet)) {
            continue;
        }
        const GEO::index_t src_corner_count = source_mesh.facets.nb_corners(src_facet);
        if (src_corner_count < 3) {
            continue;
        }
        for (GEO::index_t local_src_facet_corner = 0; local_src_facet_corner < src_corner_count; ++local_src_facet_corner) {
            const GEO::index_t src_corner         = source_mesh.facets.corner(src_facet, local_src_facet_corner);
            const GEO::index_t next_src_corner    = source_mesh.facets.corner(src_facet, (local_src_facet_corner + 1) % src_corner_count);
            const GEO::index_t local_src_edge     = local_src_facet_corner;
            const GEO::index_t opposite_src_facet = source_mesh.facets.adjacent(src_facet, local_src_edge);
            if (opposite_src_facet == GEO::NO_INDEX) {
                continue;
            }
            const GEO::index_t new_dst_facet = make_new_dst_facet_from_src_facet(src_facet, 3);
            make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 0, src_facet);
            make_new_dst_corner_from_src_corner        (new_dst_facet, 1, src_corner);
            if (is_facet_selected(opposite_src_facet)) {
                make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 2, opposite_src_facet);
            } else {
                make_new_dst_corner_from_src_corner        (new_dst_facet, 2, next_src_corner);
            }
        }
    }

    // Re-emit the unselected facets. sqrt3 inserts no edge midpoints, so this copies
    // each one 1:1 (no splicing); the kept boundary edges above weld to those copies.
    // No-op when there is no active selection.
    emit_unselected_facets_with_boundary_splice();

#if !defined(NDEBUG)
    // A pinned vertex (selection boundary, fully-unselected, or isolated) must end
    // with exactly one source (1.0, itself). Anything else means a stray ring
    // contribution leaked onto it and the boundary would crack. Interior-selected
    // vertices are smoothed (self + ring) and are not pinned, so skip them.
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

    // regeneration_flags is always the full default set: with structural_only
    // the caller (an iterated chain) has declared the regenerated-class
    // channels throwaway - the chain's final full post-processing re-derives
    // them from positions - so their interpolation is skipped as well.
    post_processing(post_process_flags(post_processing_level), default_post_process_flags);
}

void sqrt3_subdivision(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets, Component_remap* remap, const Post_processing post_processing_level)
{
    Sqrt3_subdivision operation{source, destination, selected_facets};
    operation.build(post_processing_level);
    if ((remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr)) {
        operation.remap_component_selection(*remap->source, *remap->destination);
    }
}

} // namespace erhe::geometry::operation
