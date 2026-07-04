#include "erhe_geometry/operation/subdivision/catmull_clark_subdivision.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/operation/operation_timing.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_verify/verify.hpp"

#include <cmath>

namespace erhe::geometry::operation {

class Catmull_clark_subdivision : public Geometry_operation
{
public:
    Catmull_clark_subdivision(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets);
    void build(Post_processing post_processing_level);
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

void Catmull_clark_subdivision::build(const Post_processing post_processing_level)
{
    const GEO::index_t vertex_count = source_mesh.vertices.nb();
    const GEO::index_t edge_count   = source_mesh.edges.nb();

    // Semi-sharp crease context (doc/subdivision_crease_edges.md, DeRose/Kass/
    // Truong 1998). Per-edge sharpness s drives the crease rules below; the
    // whole path is skipped (has_creases == false) when the source carries no
    // edge_sharpness values, keeping the smooth output bit-identical to the
    // pre-crease implementation.
    bool               has_creases = false;
    std::vector<float> edge_sharpness;  // per source edge; 0 = smooth, +inf = infinitely sharp
    std::vector<float> child_sharpness; // per (source edge, end vertex): [2 * edge + end]
    {
        Scoped_phase_timer phase_timer{"cc_crease_classify"};
        const Mesh_attributes& src_attributes = source.get_attributes();
        for (GEO::index_t src_edge = 0; src_edge < edge_count; ++src_edge) {
            if (src_attributes.edge_sharpness.try_get(src_edge).value_or(0.0f) > 0.0f) {
                has_creases = true;
                break;
            }
        }
        if (has_creases) {
            edge_sharpness.resize(edge_count);
            for (GEO::index_t src_edge = 0; src_edge < edge_count; ++src_edge) {
                edge_sharpness[src_edge] = std::max(src_attributes.edge_sharpness.try_get(src_edge).value_or(0.0f), 0.0f);
            }
        }
    }

    // Sharpness of the child sub-edge of src_edge incident to the given end
    // vertex, one subdivision level down. Chaikin rule per DKT Appendix B /
    // OpenSubdiv Crease::SubdivideEdgeSharpnessAtVertex: with 2+ semi-sharp
    // edges (0 < s < inf) at the end vertex, blend 3/4 own + 1/4 average of
    // the other semi-sharp edges, then decrement by 1 for the finer level and
    // clamp at 0. Smooth stays smooth, infinitely sharp stays infinitely sharp.
    const auto subdivided_sharpness_at_vertex = [&](const GEO::index_t src_edge, const GEO::index_t end_vertex) -> float {
        const float sharpness = edge_sharpness[src_edge];
        if (sharpness <= 0.0f) {
            return 0.0f;
        }
        if (std::isinf(sharpness)) {
            return sharpness;
        }
        float result = sharpness;
        const std::vector<GEO::index_t>& incident_edges = source.get_vertex_edges(end_vertex);
        if (incident_edges.size() >= 2) {
            float semi_sharp_sum   = 0.0f;
            int   semi_sharp_count = 0;
            for (GEO::index_t incident_edge : incident_edges) {
                const float incident_sharpness = edge_sharpness[incident_edge];
                if ((incident_sharpness > 0.0f) && !std::isinf(incident_sharpness)) {
                    semi_sharp_sum += incident_sharpness;
                    ++semi_sharp_count;
                }
            }
            if (semi_sharp_count > 1) {
                const float other_average = (semi_sharp_sum - sharpness) / static_cast<float>(semi_sharp_count - 1);
                result = (0.75f * sharpness) + (0.25f * other_average);
            }
        }
        result -= 1.0f;
        return (result > 0.0f) ? result : 0.0f;
    };
    if (has_creases) {
        child_sharpness.assign(2 * static_cast<std::size_t>(edge_count), 0.0f);
        for (GEO::index_t src_edge = 0; src_edge < edge_count; ++src_edge) {
            if (edge_sharpness[src_edge] <= 0.0f) {
                continue;
            }
            child_sharpness[(2 * src_edge) + 0] = subdivided_sharpness_at_vertex(src_edge, source_mesh.edges.vertex(src_edge, 0));
            child_sharpness[(2 * src_edge) + 1] = subdivided_sharpness_at_vertex(src_edge, source_mesh.edges.vertex(src_edge, 1));
        }
    }

    // Pre-size the source-vertex lookup so map_dst_vertex_from_src_vertex()
    // never grows it; the Source_tables are pre-sized after each batch
    // create below, once the destination counts are known.
    m_vertex_src_to_dst.resize(vertex_count);

    // Per-vertex classification driving the boundary-pinning rules. An
    // interior-selected vertex (all incident facets selected) gets the full
    // Catmull-Clark smoothing; every other vertex (a selection-boundary vertex, a
    // fully-unselected vertex, or an isolated/low-valence vertex) is pinned to its
    // original position so the modified region stays welded to the rest. With no
    // active selection every vertex is interior-selected, reproducing the classic
    // whole-mesh behavior.
    std::vector<uint8_t> vertex_touches_selected  (vertex_count, 0);
    std::vector<uint8_t> vertex_touches_unselected(vertex_count, 0);
    {
        Scoped_phase_timer phase_timer{"cc_classify"};
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
    }
    const auto interior_selected = [&](const GEO::index_t vertex) -> bool {
        return (vertex_touches_selected[vertex] != 0) && (vertex_touches_unselected[vertex] == 0);
    };

    //                       (n-3)P
    // Make initial P's with ------  (interior-selected vertices only; everything
    //                          n     else is pinned with weight 1.0 to itself).
    //
    // Destination vertices are batch created (one create_vertices() call
    // per phase); per-element creation is quadratic, see
    // map_dst_vertex_from_src_vertex().
    {
        Scoped_phase_timer phase_timer{"cc_initial_points"};
        const GEO::index_t first_dst_vertex = destination_mesh.vertices.create_vertices(vertex_count);
        m_dst_vertex_sources       .resize(destination_mesh.vertices.nb());
        m_dst_vertex_corner_sources.resize(destination_mesh.vertices.nb());
        for (GEO::index_t vertex : source_mesh.vertices) {
            const GEO::index_t dst_vertex = first_dst_vertex + vertex;
            const std::vector<GEO::index_t>& corners = source.get_vertex_corners(vertex);
            if (interior_selected(vertex) && (corners.size() >= 3)) {
                const float n = static_cast<float>(corners.size());
                // n = 0   -> centroid points, safe to skip
                // n = 1,2 -> ?
                // n = 3   -> ?
                const float weight = (n - 3.0f) / n;
                map_dst_vertex_from_src_vertex(dst_vertex, weight, vertex);
            } else {
                map_dst_vertex_from_src_vertex(dst_vertex, 1.0f, vertex);
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
        Scoped_phase_timer phase_timer{"cc_edge_midpoints"};
        ERHE_VERIFY(m_src_edge_to_dst_vertex.empty());

        // Batch create the midpoint vertices (per-element creation is
        // quadratic, see map_dst_vertex_from_src_vertex()): first count
        // the edges touched by the selection, then create all midpoints
        // with one call and consume them in order in the main pass.
        const auto edge_touches_selection = [this](const GEO::index_t src_edge) -> bool {
            for (GEO::index_t src_facet : source.get_edge_facets(src_edge)) {
                if (is_facet_selected(src_facet)) {
                    return true;
                }
            }
            return false;
        };
        GEO::index_t midpoint_count = 0;
        for (GEO::index_t src_edge : source_mesh.edges) {
            if (edge_touches_selection(src_edge)) {
                ++midpoint_count;
            }
        }
        GEO::index_t next_dst_vertex = destination_mesh.vertices.create_vertices(midpoint_count);
        m_dst_vertex_sources       .resize(destination_mesh.vertices.nb());
        m_dst_vertex_corner_sources.resize(destination_mesh.vertices.nb());

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
            const GEO::index_t new_dst_vertex = next_dst_vertex++;
            m_src_edge_to_dst_vertex[src_edge_key].push_back(new_dst_vertex);

            // Semi-sharp crease edge point (paper eq. 11): blend the smooth
            // edge point with the plain midpoint by t = min(sharpness, 1).
            // Interface edges are already plain midpoints, so t only applies
            // to interior edges. t == 0 takes the pre-crease code path below,
            // keeping smooth output bit-identical.
            const float crease_t = (has_creases && interior_edge) ? std::min(edge_sharpness[src_edge], 1.0f) : 0.0f;
            if (crease_t > 0.0f) {
                // Normalized masks: smooth endpoint weight is 1/(2+nf), sharp
                // is 1/2 (nf = adjacent facet count contributing a centroid).
                // Emitted weights are the blend scaled by (2+nf) so the t=0
                // limit reproduces the smooth path's integer weights exactly:
                //   endpoint: (1-t) + t*(2+nf)/2,  facet total: (1-t).
                GEO::index_t adjacent_facet_count = 0;
                for (GEO::index_t src_facet : src_edge_facets) {
                    if (source_mesh.facets.nb_corners(src_facet) > 0) {
                        ++adjacent_facet_count;
                    }
                }
                const float endpoint_weight = (1.0f - crease_t) + (crease_t * 0.5f * (2.0f + static_cast<float>(adjacent_facet_count)));
                add_vertex_source(new_dst_vertex, endpoint_weight, src_vertex_a);
                add_vertex_source(new_dst_vertex, endpoint_weight, src_vertex_b);
                for (GEO::index_t src_facet : src_edge_facets) {
                    const GEO::index_t src_facet_corner_count = source_mesh.facets.nb_corners(src_facet);
                    if (src_facet_corner_count == 0) {
                        continue;
                    }
                    if (crease_t < 1.0f) {
                        const float facet_weight = (1.0f - crease_t) / static_cast<float>(src_facet_corner_count);
                        add_facet_centroid(new_dst_vertex, facet_weight, src_facet);
                    }
                    // Corner sources: the centroid contribution above supplies
                    // them scaled by (1-t); blend in the endpoints' corners
                    // scaled by t (the interface-edge pattern) so a fully
                    // sharp midpoint still inherits texcoords / colors.
                    const GEO::index_t local_a  = source_mesh.facets.find_vertex(src_facet, src_vertex_a);
                    const GEO::index_t local_b  = source_mesh.facets.find_vertex(src_facet, src_vertex_b);
                    const GEO::index_t corner_a = source_mesh.facets.corner(src_facet, local_a);
                    const GEO::index_t corner_b = source_mesh.facets.corner(src_facet, local_b);
                    add_vertex_corner_source(new_dst_vertex, crease_t, corner_a);
                    add_vertex_corner_source(new_dst_vertex, crease_t, corner_b);
                }
            } else {
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
            } // crease_t == 0 (pre-crease code path)

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
        Scoped_phase_timer phase_timer{"cc_facet_centroids"};
        m_src_facet_centroid_to_dst_vertex.resize(source_mesh.facets.nb());

        // Batch create the centroid vertices (per-element creation is
        // quadratic, see map_dst_vertex_from_src_facet_centroid()).
        GEO::index_t centroid_count = 0;
        for (GEO::index_t src_facet : source_mesh.facets) {
            if (is_facet_selected(src_facet)) {
                ++centroid_count;
            }
        }
        GEO::index_t next_centroid_dst_vertex = destination_mesh.vertices.create_vertices(centroid_count);
        m_dst_vertex_sources       .resize(destination_mesh.vertices.nb());
        m_dst_vertex_corner_sources.resize(destination_mesh.vertices.nb());

        for (GEO::index_t src_facet : source_mesh.facets) {
            if (!is_facet_selected(src_facet)) {
                continue;
            }
            map_dst_vertex_from_src_facet_centroid(next_centroid_dst_vertex++, src_facet);

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
    //
    // The quads are batch created (one create_quads() call); per-element
    // creation is quadratic, see map_dst_facet_from_src_facet().
    {
        Scoped_phase_timer phase_timer{"cc_quads"};
        GEO::index_t quad_count = 0;
        for (GEO::index_t src_facet : source_mesh.facets) {
            if (!is_facet_selected(src_facet)) {
                continue;
            }
            const GEO::index_t corner_count = source_mesh.facets.nb_vertices(src_facet);
            if (corner_count < 3) {
                continue;
            }
            quad_count += corner_count;
        }
        const GEO::index_t first_dst_facet = destination_mesh.facets.create_quads(quad_count);
        m_dst_facet_sources .resize(destination_mesh.facets.nb());
        m_dst_corner_sources.resize(destination_mesh.facet_corners.nb());
        GEO::index_t next_dst_facet = first_dst_facet;

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
                const GEO::index_t new_dst_facet           = next_dst_facet++;
                map_dst_facet_from_src_facet(new_dst_facet, src_facet);
                make_new_dst_corner_from_src_facet_centroid(new_dst_facet, 0, src_facet);
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 1, previous_edge_midpoint);
                make_new_dst_corner_from_src_corner        (new_dst_facet, 2, src_corner);
                make_new_dst_corner_from_dst_vertex        (new_dst_facet, 3, next_edge_midpoint);
            }
        }
        // Every batch created quad must have been consumed; a mismatch
        // would leave degenerate all-zero quads in the mesh.
        ERHE_VERIFY(next_dst_facet == (first_dst_facet + quad_count));
    }

    // Crease vertex-point masks. The three phases above assembled the smooth
    // vertex mask incrementally; for vertices with 2+ incident sharp edges,
    // rewrite the accumulated Source_table entry into the blend of the parent
    // and child rule masks (OpenSubdiv Scheme::ComputeVertexVertexMask
    // semantics resolving DKT Appendix B): parent rule from the incident
    // sharp-edge count (2 = crease, 3+ = corner), child rule from the
    // subdivided sharpnesses at this vertex, fractional weight = clamped
    // average of the sharpness values that decayed to zero across the step.
    // Masks: crease = 1/8 far_j + 6/8 self + 1/8 far_k (paper eq. 9), corner
    // = self (eq. 10). All mask components are scaled by the smooth entry's
    // weight sum so the differing implicit normalizations combine correctly.
    // Selection-pinned and low-valence vertices keep their pinned mask
    // (pinning wins; both crease-ish rules are compatible with a pin).
    if (has_creases) {
        Scoped_phase_timer phase_timer{"cc_crease_vertex_masks"};
        for (GEO::index_t src_vertex : source_mesh.vertices) {
            if (!interior_selected(src_vertex)) {
                continue;
            }
            if (source.get_vertex_corners(src_vertex).size() < 3) {
                continue; // pinned by the initial-points phase
            }
            const std::vector<GEO::index_t>& incident_edges = source.get_vertex_edges(src_vertex);

            int          parent_sharp_count    = 0;
            GEO::index_t parent_sharp_edges[2] = { GEO::NO_EDGE, GEO::NO_EDGE };
            for (GEO::index_t incident_edge : incident_edges) {
                if (edge_sharpness[incident_edge] > 0.0f) {
                    if (parent_sharp_count < 2) {
                        parent_sharp_edges[parent_sharp_count] = incident_edge;
                    }
                    ++parent_sharp_count;
                }
            }
            if (parent_sharp_count < 2) {
                continue; // smooth or dart: the smooth mask stands
            }

            const auto child_end_sharpness = [&](const GEO::index_t incident_edge) -> float {
                const GEO::index_t end = (source_mesh.edges.vertex(incident_edge, 0) == src_vertex) ? 0 : 1;
                return child_sharpness[(2 * incident_edge) + end];
            };
            int          child_sharp_count    = 0;
            GEO::index_t child_sharp_edges[2] = { GEO::NO_EDGE, GEO::NO_EDGE };
            for (GEO::index_t incident_edge : incident_edges) {
                if (child_end_sharpness(incident_edge) > 0.0f) {
                    if (child_sharp_count < 2) {
                        child_sharp_edges[child_sharp_count] = incident_edge;
                    }
                    ++child_sharp_count;
                }
            }

            enum class Rule : int { smooth = 0, crease = 1, corner = 2 };
            const Rule parent_rule = (parent_sharp_count == 2) ? Rule::crease : Rule::corner;
            const Rule child_rule  = (child_sharp_count  <  2) ? Rule::smooth : ((child_sharp_count == 2) ? Rule::crease : Rule::corner);

            float parent_weight = 1.0f;
            if (child_rule != parent_rule) {
                float transition_sum   = 0.0f;
                int   transition_count = 0;
                for (GEO::index_t incident_edge : incident_edges) {
                    const float s = edge_sharpness[incident_edge];
                    if ((s > 0.0f) && !std::isinf(s) && (child_end_sharpness(incident_edge) <= 0.0f)) {
                        transition_sum += s;
                        ++transition_count;
                    }
                }
                parent_weight = (transition_count > 0) ? std::min(transition_sum / static_cast<float>(transition_count), 1.0f) : 0.0f;
            }

            float w_smooth = 0.0f;
            float w_crease = 0.0f;
            float w_corner = 0.0f;
            const auto add_rule_weight = [&](const Rule rule, const float weight) {
                switch (rule) {
                    case Rule::smooth: w_smooth += weight; break;
                    case Rule::crease: w_crease += weight; break;
                    case Rule::corner: w_corner += weight; break;
                }
            };
            add_rule_weight(parent_rule, parent_weight);
            if (child_rule != parent_rule) {
                add_rule_weight(child_rule, 1.0f - parent_weight);
            }

            const GEO::index_t dst_vertex = m_vertex_src_to_dst[src_vertex];
            std::vector<std::pair<float, GEO::index_t>>& sources = m_dst_vertex_sources.data()[dst_vertex];
            float smooth_sum = 0.0f;
            for (const std::pair<float, GEO::index_t>& entry : sources) {
                smooth_sum += entry.first;
            }
            ERHE_VERIFY(smooth_sum > 0.0f);
            if (w_smooth <= 0.0f) {
                sources.clear();
            } else if (w_smooth < 1.0f) {
                for (std::pair<float, GEO::index_t>& entry : sources) {
                    entry.first *= w_smooth;
                }
            }
            if (w_crease > 0.0f) {
                // Far endpoints come from whichever rule is the crease; when
                // the rules differ at most one of them is a crease.
                const GEO::index_t* crease_edges = (parent_rule == Rule::crease) ? parent_sharp_edges : child_sharp_edges;
                const auto far_vertex = [&](const GEO::index_t crease_edge) -> GEO::index_t {
                    const GEO::index_t a = source_mesh.edges.vertex(crease_edge, 0);
                    return (a == src_vertex) ? source_mesh.edges.vertex(crease_edge, 1) : a;
                };
                add_vertex_source(dst_vertex, 0.75f  * smooth_sum * w_crease, src_vertex);
                add_vertex_source(dst_vertex, 0.125f * smooth_sum * w_crease, far_vertex(crease_edges[0]));
                add_vertex_source(dst_vertex, 0.125f * smooth_sum * w_crease, far_vertex(crease_edges[1]));
            }
            if (w_corner > 0.0f) {
                add_vertex_source(dst_vertex, smooth_sum * w_corner, src_vertex);
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

    // regeneration_flags is always the full default set: with structural_only
    // the caller (an iterated chain) has declared the regenerated-class
    // channels throwaway - the chain's final full post-processing re-derives
    // them from positions - so their interpolation is skipped as well.
    post_processing(post_process_flags(post_processing_level), default_post_process_flags);

    // Propagate child sharpness onto the destination edges. This must run
    // after post_processing(): the destination edge store only exists once
    // process() has run build_edges() (both post-processing levels include
    // it). m_vertex_src_to_dst stays valid across sanitize(): it only ever
    // deletes degenerate facets, which the quad construction above does not
    // produce. Zero child values are not written, keeping the destination
    // attribute sparse.
    if (has_creases) {
        Scoped_phase_timer phase_timer{"cc_crease_propagate"};
        for (GEO::index_t src_edge = 0; src_edge < edge_count; ++src_edge) {
            if (edge_sharpness[src_edge] <= 0.0f) {
                continue;
            }
            const GEO::index_t src_vertex_a = source_mesh.edges.vertex(src_edge, 0);
            const GEO::index_t src_vertex_b = source_mesh.edges.vertex(src_edge, 1);
            const GEO::index_t dst_vertex_a = m_vertex_src_to_dst[src_vertex_a];
            const GEO::index_t dst_vertex_b = m_vertex_src_to_dst[src_vertex_b];
            const GEO::index_t midpoint     = get_src_edge_new_vertex(src_vertex_a, src_vertex_b, 0);
            if (midpoint == GEO::NO_VERTEX) {
                // Edge untouched by the selection: the facets around it were
                // re-emitted unsplit, so the sharpness carries unchanged.
                destination.set_edge_sharpness(dst_vertex_a, dst_vertex_b, edge_sharpness[src_edge]);
                continue;
            }
            const float child_a = child_sharpness[(2 * src_edge) + 0];
            const float child_b = child_sharpness[(2 * src_edge) + 1];
            if (child_a > 0.0f) {
                destination.set_edge_sharpness(dst_vertex_a, midpoint, child_a);
            }
            if (child_b > 0.0f) {
                destination.set_edge_sharpness(midpoint, dst_vertex_b, child_b);
            }
        }
    }
}

void catmull_clark_subdivision(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets, Component_remap* remap, const Post_processing post_processing_level)
{
    Catmull_clark_subdivision operation{source, destination, selected_facets};
    operation.build(post_processing_level);
    if ((remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr)) {
        operation.remap_component_selection(*remap->source, *remap->destination);
    }
}

} // namespace erhe::geometry::operation
