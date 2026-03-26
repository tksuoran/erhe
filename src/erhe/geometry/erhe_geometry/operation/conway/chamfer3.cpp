#include "erhe_geometry/operation/conway/chamfer3.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::geometry::operation {

class Chamfer3 : public Geometry_operation
{
public:
    Chamfer3(const Geometry& source, Geometry& destination, float bevel_ratio);
    void build();
    float m_bevel_ratio;
};

Chamfer3::Chamfer3(const Geometry& source, Geometry& destination, float bevel_ratio)
    : Geometry_operation{source, destination}
    , m_bevel_ratio{bevel_ratio}
{
}

namespace {

void assert_elements_are_unique(std::initializer_list<GEO::index_t> v)
{
    for (GEO::index_t index : v) {
        ERHE_VERIFY(index != GEO::NO_INDEX);
    }
    std::set<GEO::index_t> set{v};
    ERHE_VERIFY(set.size() == v.size());
}

} // anonymous namespace

void Chamfer3::build()
{
    const float bevel_ratio = m_bevel_ratio;

    // Step 1: Compute corner positions by lerping each face vertex
    // toward the face centroid. Each edge E of face F becomes E' with
    // length (1 - bevel_ratio) * length(E).
    std::vector<GEO::vec3f> corner_inset_positions;
    corner_inset_positions.resize(source_mesh.facet_corners.nb());

    for (GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t corner_count = source_mesh.facets.nb_corners(src_facet);
        const GEO::vec3f   centroid     = mesh_facet_centerf(source_mesh, src_facet);

        for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
            const GEO::index_t corner = source_mesh.facets.corner(src_facet, lc);
            const GEO::index_t vertex = source_mesh.facet_corners.vertex(corner);
            const GEO::vec3f   pos    = get_pointf(source_mesh.vertices, vertex);
            corner_inset_positions[corner] = pos + bevel_ratio * (centroid - pos);
        }
    }

    // Step 2: For each source edge (which becomes a hexagonal face),
    // compute the best-fit plane from the 4 known corner positions.
    // The hexagon has 6 vertices: 4 corners (from the two adjacent
    // shrunk faces) + 2 "new vertices" (at the original edge endpoints).
    // We know the 4 corners; the 2 new vertices are computed in step 3.
    //
    // Store the plane (normal, d) for each edge's hexagonal face.
    struct HexPlane {
        GEO::vec3f normal{0.0f, 0.0f, 0.0f};
        float      d{0.0f};
    };
    std::vector<HexPlane> hex_planes(source_mesh.edges.nb());

    for (GEO::index_t src_edge : source_mesh.edges) {
        const std::vector<GEO::index_t>& edge_facets = source.get_edge_facets(src_edge);
        if (edge_facets.size() != 2) {
            continue;
        }
        const GEO::index_t lo_vertex              = source_mesh.edges.vertex(src_edge, 0);
        const GEO::index_t hi_vertex              = source_mesh.edges.vertex(src_edge, 1);
        const GEO::index_t facet0                 = edge_facets[0];
        const GEO::index_t facet1                 = edge_facets[1];
        const GEO::index_t facet0_lo_local_corner = source_mesh.facets.find_edge(facet0, lo_vertex, hi_vertex);
        const GEO::index_t facet0_hi_local_corner = source_mesh.facets.find_edge(facet0, hi_vertex, lo_vertex);
        const bool         facet0_increasing      = facet0_lo_local_corner != GEO::NO_INDEX;
        if (!facet0_increasing && (facet0_hi_local_corner == GEO::NO_INDEX)) {
            continue; // edge not found in facet0 — skip inconsistent edge
        }
        const GEO::index_t facet0_corner          = source_mesh.facets.corner(facet0, facet0_increasing ? facet0_lo_local_corner : facet0_hi_local_corner);
        const GEO::index_t facet1_lo_local_corner = source_mesh.facets.find_edge(facet1, lo_vertex, hi_vertex);
        const GEO::index_t facet1_hi_local_corner = source_mesh.facets.find_edge(facet1, hi_vertex, lo_vertex);
        const bool         facet1_increasing      = facet1_lo_local_corner != GEO::NO_INDEX;
        if (!facet1_increasing && (facet1_hi_local_corner == GEO::NO_INDEX)) {
            continue; // edge not found in facet1 — skip inconsistent edge
        }
        const GEO::index_t facet1_corner          = source_mesh.facets.corner(facet1, facet1_increasing ? facet1_lo_local_corner : facet1_hi_local_corner);
        const GEO::index_t facet1_next_corner = source_mesh.facets.next_corner_around_facet(facet1, facet1_corner);
        const GEO::index_t facet0_next_corner = source_mesh.facets.next_corner_around_facet(facet0, facet0_corner);

        // The 4 known corner positions
        const GEO::vec3f c0 = corner_inset_positions[facet0_corner];
        const GEO::vec3f c1 = corner_inset_positions[facet0_next_corner];
        const GEO::vec3f c2 = corner_inset_positions[facet1_corner];
        const GEO::vec3f c3 = corner_inset_positions[facet1_next_corner];

        // Best-fit plane from 4 points: centroid + normal from cross products
        const GEO::vec3f center = 0.25f * (c0 + c1 + c2 + c3);
        // Use the two diagonals to compute the normal
        const GEO::vec3f diag1 = c2 - c0;
        const GEO::vec3f diag2 = c3 - c1;
        GEO::vec3f normal = GEO::cross(diag1, diag2);
        const float len = GEO::length(normal);
        if (len > 1e-7f) {
            normal = normal / len;
        } else {
            // Degenerate — use average of adjacent face normals
            normal = GEO::normalize(
                mesh_facet_normalf(source_mesh, facet0) +
                mesh_facet_normalf(source_mesh, facet1)
            );
        }
        hex_planes[src_edge].normal = normal;
        hex_planes[src_edge].d = -GEO::dot(normal, center);
    }

    // Step 3: For each source vertex V, compute V' on the ray through
    // the corner-loop polygon centroid along the polygon normal.
    //
    // The "corner loop" is the closed polygon formed by V's corner
    // positions (one per adjacent face). Each edge of this polygon is
    // the open edge of a hex quad waiting for the new vertex.
    //
    // V' is constrained to lie on: P(t) = centroid + t * normal
    // where t is chosen by LS fitting to the hex target planes:
    //   minimize Σ(n_i · P(t) + d_i)²
    //   = minimize Σ(a_i + t * b_i)²  where a_i = n_i·centroid + d_i, b_i = n_i·normal
    //   => t = -Σ(a_i * b_i) / Σ(b_i²)
    for (GEO::index_t src_vertex : source_mesh.vertices) {
        const GEO::index_t new_dst_vertex = destination_mesh.vertices.create_vertex();
        const GEO::vec3f V = get_pointf(source_mesh.vertices, src_vertex);
        const std::vector<GEO::index_t>& v_corners = source.get_vertex_corners(src_vertex);
        const std::vector<GEO::index_t>& v_edges   = source.get_vertex_edges(src_vertex);

        if (v_corners.empty() || v_edges.empty()) {
            set_pointf(destination_mesh.vertices, new_dst_vertex, V);
            continue;
        }

        // Polygon centroid: average of all corner inset positions at V
        GEO::vec3f centroid{0.0f, 0.0f, 0.0f};
        for (GEO::index_t corner : v_corners) {
            centroid = centroid + corner_inset_positions[corner];
        }
        centroid = centroid * (1.0f / static_cast<float>(v_corners.size()));

        // Polygon normal: average of adjacent source face normals,
        // oriented to point outward (from centroid toward V)
        GEO::vec3f normal{0.0f, 0.0f, 0.0f};
        for (GEO::index_t corner : v_corners) {
            const GEO::index_t facet = source.get_corner_facet(corner);
            normal = normal + mesh_facet_normalf(source_mesh, facet);
        }
        const float normal_len = GEO::length(normal);
        if (normal_len < 1e-10f) {
            set_pointf(destination_mesh.vertices, new_dst_vertex, centroid);
            continue;
        }
        normal = normal / normal_len;
        // Orient: normal should point from centroid toward V
        if (GEO::dot(normal, V - centroid) < 0.0f) {
            normal = -1.0f * normal;
        }

        // LS fit along ray P(t) = centroid + t * normal
        // minimize Σ(n_i · (centroid + t*normal) + d_i)²
        // a_i = n_i · centroid + d_i  (signed distance from centroid to hex plane i)
        // b_i = n_i · normal          (projection of hex normal onto polygon normal)
        // t = -Σ(a_i * b_i) / Σ(b_i²)
        float sum_ab = 0.0f;
        float sum_bb = 0.0f;
        for (GEO::index_t e : v_edges) {
            const GEO::vec3f n = hex_planes[e].normal;
            const float      d = hex_planes[e].d;
            const float a = GEO::dot(n, centroid) + d;
            const float b = GEO::dot(n, normal);
            sum_ab += a * b;
            sum_bb += b * b;
        }

        float t = 0.0f;
        if (sum_bb > 1e-10f) {
            t = -sum_ab / sum_bb;
        }

        const GEO::vec3f new_pos = centroid + t * normal;
        set_pointf(destination_mesh.vertices, new_dst_vertex, new_pos);

        // Debug: draw the loop of hex quad open edges around this vertex.
        // For each adjacent edge E, the hex quad has an "open edge"
        // connecting V's two corner positions (one per face sharing E).
        // These edges form a closed polygon — the boundary where the
        // new vertex must be placed.
        for (GEO::index_t edge : v_edges) {
            const std::vector<GEO::index_t>& edge_facets = source.get_edge_facets(edge);
            if (edge_facets.size() != 2) {
                continue;
            }
            const GEO::index_t facet0 = edge_facets[0];
            const GEO::index_t facet1 = edge_facets[1];

            // Find corner of src_vertex on each face
            GEO::vec3f c0{0.0f, 0.0f, 0.0f};
            GEO::vec3f c1{0.0f, 0.0f, 0.0f};
            bool found0 = false;
            bool found1 = false;
            const GEO::index_t f0_count = source_mesh.facets.nb_corners(facet0);
            for (GEO::index_t lc = 0; lc < f0_count; ++lc) {
                const GEO::index_t c = source_mesh.facets.corner(facet0, lc);
                if (source_mesh.facet_corners.vertex(c) == src_vertex) {
                    c0 = corner_inset_positions[c];
                    found0 = true;
                    break;
                }
            }
            const GEO::index_t f1_count = source_mesh.facets.nb_corners(facet1);
            for (GEO::index_t lc = 0; lc < f1_count; ++lc) {
                const GEO::index_t c = source_mesh.facets.corner(facet1, lc);
                if (source_mesh.facet_corners.vertex(c) == src_vertex) {
                    c1 = corner_inset_positions[c];
                    found1 = true;
                    break;
                }
            }
#if 0
            if (found0 && found1) {
                destination.add_debug_line(new_dst_vertex, GEO::NO_INDEX,
                    to_glm_vec3(c0), to_glm_vec3(c1),
                    glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
            }
#endif
        }
    }

    // Step 4: Create output mesh topology (same as reference chamfer)
    // Shrunk face polygons
    std::vector<GEO::index_t> src_corner_to_dst_vertex;
    src_corner_to_dst_vertex.resize(source_mesh.facet_corners.nb());
    std::fill(src_corner_to_dst_vertex.begin(), src_corner_to_dst_vertex.end(), GEO::NO_INDEX);
    for (GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t corner_count  = source_mesh.facets.nb_corners(src_facet);
        const GEO::index_t new_dst_facet = destination_mesh.facets.create_polygon(corner_count);
        for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
            const GEO::index_t corner           = source_mesh.facets.corner(src_facet, lc);
            const GEO::index_t dst_corner_vertex = destination_mesh.vertices.create_vertex();
            src_corner_to_dst_vertex[corner] = dst_corner_vertex;
            destination_mesh.facets.set_vertex(new_dst_facet, lc, dst_corner_vertex);
            set_pointf(destination_mesh.vertices, dst_corner_vertex, corner_inset_positions[corner]);
        }
#if 0
        // Debug: blue lines for source face F edges on destination shrunk face F'
        for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
            const GEO::index_t corner      = source_mesh.facets.corner(src_facet, lc);
            const GEO::index_t next_corner = source_mesh.facets.corner(src_facet, (lc + 1) % corner_count);
            const GEO::index_t v0          = source_mesh.facet_corners.vertex(corner);
            const GEO::index_t v1          = source_mesh.facet_corners.vertex(next_corner);
            const glm::vec3 src_p0 = to_glm_vec3(get_pointf(source_mesh.vertices, v0));
            const glm::vec3 src_p1 = to_glm_vec3(get_pointf(source_mesh.vertices, v1));
            destination.add_debug_line(src_corner_to_dst_vertex[corner], new_dst_facet, src_p0, src_p1, glm::vec4{0.3f, 0.5f, 1.0f, 1.0f}, 1.0f);
        }
#endif
    }

    // Hexagonal faces at edges
    for (GEO::index_t src_edge : source_mesh.edges) {
        const std::vector<GEO::index_t>& edge_facets = source.get_edge_facets(src_edge);
        if (edge_facets.size() != 2) {
            continue;
        }
        const GEO::index_t lo_vertex              = source_mesh.edges.vertex(src_edge, 0);
        const GEO::index_t hi_vertex              = source_mesh.edges.vertex(src_edge, 1);
        const GEO::index_t facet0                 = edge_facets[0];
        const GEO::index_t facet1                 = edge_facets[1];
        const GEO::index_t facet0_lo_local_corner = source_mesh.facets.find_edge(facet0, lo_vertex, hi_vertex);
        const GEO::index_t facet0_hi_local_corner = source_mesh.facets.find_edge(facet0, hi_vertex, lo_vertex);
        const bool         facet0_increasing      = facet0_lo_local_corner != GEO::NO_INDEX;
        const bool         facet0_decreasing      = facet0_hi_local_corner != GEO::NO_INDEX;
        if (!facet0_increasing && !facet0_decreasing) {
            continue;
        }
        const GEO::index_t facet0_corner          = source_mesh.facets.corner(facet0, facet0_increasing ? facet0_lo_local_corner : facet0_hi_local_corner);
        const GEO::index_t facet1_lo_local_corner = source_mesh.facets.find_edge(facet1, lo_vertex, hi_vertex);
        const GEO::index_t facet1_hi_local_corner = source_mesh.facets.find_edge(facet1, hi_vertex, lo_vertex);
        const bool         facet1_increasing      = facet1_lo_local_corner != GEO::NO_INDEX;
        const bool         facet1_decreasing      = facet1_hi_local_corner != GEO::NO_INDEX;
        if (!facet1_increasing && !facet1_decreasing) {
            continue;
        }
        const GEO::index_t facet1_corner          = source_mesh.facets.corner(facet1, facet1_increasing ? facet1_lo_local_corner : facet1_hi_local_corner);
        ERHE_VERIFY(facet0_increasing != facet0_decreasing);
        ERHE_VERIFY(facet1_increasing != facet1_decreasing);
        ERHE_VERIFY(facet0_increasing == facet1_decreasing);
        const GEO::index_t new_dst_facet      = destination_mesh.facets.create_polygon(6);
        const GEO::index_t facet1_next_corner = source_mesh.facets.next_corner_around_facet(facet1, facet1_corner);
        const GEO::index_t facet1_next_vertex = src_corner_to_dst_vertex[facet1_next_corner];
        const GEO::index_t facet1_vertex      = src_corner_to_dst_vertex[facet1_corner];
        const GEO::index_t facet0_next_corner = source_mesh.facets.next_corner_around_facet(facet0, facet0_corner);
        const GEO::index_t facet0_next_vertex = src_corner_to_dst_vertex[facet0_next_corner];
        const GEO::index_t facet0_vertex      = src_corner_to_dst_vertex[facet0_corner];
        if (facet0_increasing) {
            assert_elements_are_unique({lo_vertex, facet1_next_vertex, facet1_vertex, hi_vertex, facet0_next_vertex, facet0_vertex});
            destination_mesh.facets.set_vertex(new_dst_facet, 0, lo_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 1, facet1_next_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 2, facet1_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 3, hi_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 4, facet0_next_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 5, facet0_vertex);
        } else {
            assert_elements_are_unique({hi_vertex, facet1_next_vertex, facet1_vertex, lo_vertex, facet0_next_vertex, facet0_vertex});
            destination_mesh.facets.set_vertex(new_dst_facet, 0, hi_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 1, facet1_next_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 2, facet1_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 3, lo_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 4, facet0_next_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 5, facet0_vertex);
        }
#if 0
        // Debug: green line for source edge E on destination hexagon
        const glm::vec3 src_lo = to_glm_vec3(get_pointf(source_mesh.vertices, lo_vertex));
        const glm::vec3 src_hi = to_glm_vec3(get_pointf(source_mesh.vertices, hi_vertex));
        destination.add_debug_line(lo_vertex, new_dst_facet, src_lo, src_hi, glm::vec4{0.2f, 1.0f, 0.3f, 1.0f}, 1.0f);
#endif
    }

#if 0
    // Debug visualization: for each source facet F, draw the shrunk face
    // edges (E') in white and ALL remaining edges of the associated
    // hexagon in orange (including the edges on the adjacent face's side).
    for (GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t corner_count = source_mesh.facets.nb_corners(src_facet);
        for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
            const GEO::index_t corner      = source_mesh.facets.corner(src_facet, lc);
            const GEO::index_t next_corner = source_mesh.facets.corner(src_facet, (lc + 1) % corner_count);
            const GEO::index_t vertex      = source_mesh.facet_corners.vertex(corner);
            const GEO::index_t next_vertex = source_mesh.facet_corners.vertex(next_corner);

            // E' edge (shrunk face edge) in white
            const glm::vec3 p0 = to_glm_vec3(corner_inset_positions[corner]);
            const glm::vec3 p1 = to_glm_vec3(corner_inset_positions[next_corner]);
            source.add_debug_line(vertex, src_facet, p0, p1, glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);

            // Inset vertex positions (LS-fitted vertices replacing original vertices)
            const glm::vec3 inset_v  = to_glm_vec3(get_pointf(destination_mesh.vertices, vertex));
            const glm::vec3 inset_nv = to_glm_vec3(get_pointf(destination_mesh.vertices, next_vertex));

            // Labels (always drawn regardless of edge lookup)
            source.add_debug_text(vertex, src_facet, p0, 0xffffffff,
                fmt::format("F{}:C{}", src_facet, lc));
            source.add_debug_text(vertex, src_facet, inset_v, 0xff8800ff,
                fmt::format("V{}", vertex));

            // Find the source edge E between vertex and next_vertex
            const GEO::index_t src_edge = source.get_edge(vertex, next_vertex);
            if (src_edge == GEO::NO_EDGE) {
                // Red marker at midpoint to flag missing edge
                const glm::vec3 mid = 0.5f * (to_glm_vec3(get_pointf(source_mesh.vertices, vertex)) +
                                               to_glm_vec3(get_pointf(source_mesh.vertices, next_vertex)));
                source.add_debug_text(vertex, src_facet, mid, 0xff0000ff,
                    fmt::format("NO_EDGE V{}-V{}", vertex, next_vertex));
                continue;
            }

            // Find the adjacent face sharing this edge
            const std::vector<GEO::index_t>& edge_facets = source.get_edge_facets(src_edge);
            if (edge_facets.size() != 2) {
                continue;
            }
            const GEO::index_t adj_facet = (edge_facets[0] == src_facet) ? edge_facets[1] : edge_facets[0];

            // Find the corners on the adjacent face for this edge
            const GEO::index_t adj_lo_local = source_mesh.facets.find_edge(adj_facet, vertex, next_vertex);
            const GEO::index_t adj_hi_local = source_mesh.facets.find_edge(adj_facet, next_vertex, vertex);
            const bool adj_increasing = (adj_lo_local != GEO::NO_INDEX);
            const GEO::index_t adj_corner = source_mesh.facets.corner(
                adj_facet, adj_increasing ? adj_lo_local : adj_hi_local
            );
            const GEO::index_t adj_next_corner = source_mesh.facets.next_corner_around_facet(adj_facet, adj_corner);

            const glm::vec3 adj_p0 = to_glm_vec3(corner_inset_positions[adj_corner]);
            const glm::vec3 adj_p1 = to_glm_vec3(corner_inset_positions[adj_next_corner]);

            // Hexagon has 6 edges: 2 E' edges (one per face) + 4 side edges.
            // We already drew THIS face's E' in white. Draw the remaining 5:
            //   4 side edges (corners <-> inset vertices) in orange
            //   1 opposite E' (adjacent face's shrunk edge) in orange
            source.add_debug_line(vertex, src_facet, p0, inset_v, glm::vec4{1.0f, 0.5f, 0.0f, 1.0f}, 1.0f);
            source.add_debug_line(next_vertex, src_facet, p1, inset_nv, glm::vec4{1.0f, 0.5f, 0.0f, 1.0f}, 1.0f);
            // Opposite E' (adjacent face's shrunk edge)
            source.add_debug_line(vertex, src_facet, adj_p0, adj_p1, glm::vec4{1.0f, 0.5f, 0.0f, 1.0f}, 1.0f);
            if (adj_increasing) {
                // adj_corner is at vertex, adj_next_corner is at next_vertex
                source.add_debug_line(vertex, src_facet, adj_p0, inset_v, glm::vec4{1.0f, 0.5f, 0.0f, 1.0f}, 1.0f);
                source.add_debug_line(next_vertex, src_facet, adj_p1, inset_nv, glm::vec4{1.0f, 0.5f, 0.0f, 1.0f}, 1.0f);
            } else {
                // adj_corner is at next_vertex, adj_next_corner is at vertex
                source.add_debug_line(next_vertex, src_facet, adj_p0, inset_nv, glm::vec4{1.0f, 0.5f, 0.0f, 1.0f}, 1.0f);
                source.add_debug_line(vertex, src_facet, adj_p1, inset_v, glm::vec4{1.0f, 0.5f, 0.0f, 1.0f}, 1.0f);
            }
        }
    }
#endif

    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    destination.process(flags);
}

void chamfer3(const Geometry& source, Geometry& destination, float bevel_ratio)
{
    Chamfer3 operation{source, destination, bevel_ratio};
    operation.build();
}

} // namespace erhe::geometry::operation
