#include "erhe_geometry/operation/conway/chamfer3.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <vector>

namespace erhe::geometry::operation {

class Chamfer3 : public Geometry_operation
{
public:
    Chamfer3(const Geometry& source, Geometry& destination, float bevel_ratio, const std::set<GEO::index_t>* selected_facets);
    void build();
    float m_bevel_ratio;
};

Chamfer3::Chamfer3(const Geometry& source, Geometry& destination, float bevel_ratio, const std::set<GEO::index_t>* selected_facets)
    : Geometry_operation{source, destination}
    , m_bevel_ratio{bevel_ratio}
{
    m_selected_facets = selected_facets;
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

    // Classify each source vertex relative to the selection (mirrors truncate.cpp).
    // interior_selected(v): every facet incident to v is selected, so v may move to
    // its least-squares-fitted V' position. A boundary vertex (incident to both a
    // selected and an unselected facet) and a fully unselected vertex are pinned at
    // their original position so the modified region stays welded to the remainder.
    // With no active selection every vertex is interior, reproducing the whole-mesh
    // behavior (every vertex fitted).
    const GEO::index_t vertex_count = source_mesh.vertices.nb();
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

    // Step 1: Compute corner positions by lerping each face vertex toward the face
    // centroid (only for selected facets - unselected facet corners are never read).
    // Each edge E of face F becomes E' with length (1 - bevel_ratio) * length(E).
    std::vector<GEO::vec3f> corner_inset_positions;
    corner_inset_positions.resize(source_mesh.facet_corners.nb());

    for (GEO::index_t src_facet : source_mesh.facets) {
        if (!is_facet_selected(src_facet)) {
            continue;
        }
        const GEO::index_t corner_count = source_mesh.facets.nb_corners(src_facet);
        const GEO::vec3f   centroid     = mesh_facet_centerf(source_mesh, src_facet);

        for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
            const GEO::index_t corner = source_mesh.facets.corner(src_facet, lc);
            const GEO::index_t vertex = source_mesh.facet_corners.vertex(corner);
            const GEO::vec3f   pos    = get_pointf(source_mesh.vertices, vertex);
            corner_inset_positions[corner] = pos + bevel_ratio * (centroid - pos);
        }
    }

    // Step 2: For each interior source edge (both adjacent facets selected, which
    // becomes a hexagonal face) compute the best-fit plane from the 4 known corner
    // positions. The hexagon has 6 vertices: 4 corners (from the two adjacent shrunk
    // faces) + 2 "new vertices" (at the original edge endpoints). The 2 new vertices
    // are computed in step 3. Only interior edges need a plane: the V' fit at an
    // interior vertex only ever reads its incident interior edges (an interior vertex
    // has only interior incident edges).
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
        const GEO::index_t facet0 = edge_facets[0];
        const GEO::index_t facet1 = edge_facets[1];
        if (!is_facet_selected(facet0) || !is_facet_selected(facet1)) {
            continue; // Not an interior edge: no hexagon, no plane.
        }
        const GEO::index_t lo_vertex              = source_mesh.edges.vertex(src_edge, 0);
        const GEO::index_t hi_vertex              = source_mesh.edges.vertex(src_edge, 1);
        const GEO::index_t facet0_lo_local_corner = source_mesh.facets.find_edge(facet0, lo_vertex, hi_vertex);
        const GEO::index_t facet0_hi_local_corner = source_mesh.facets.find_edge(facet0, hi_vertex, lo_vertex);
        const bool         facet0_increasing      = facet0_lo_local_corner != GEO::NO_INDEX;
        if (!facet0_increasing && (facet0_hi_local_corner == GEO::NO_INDEX)) {
            continue; // edge not found in facet0 - skip inconsistent edge
        }
        const GEO::index_t facet0_corner          = source_mesh.facets.corner(facet0, facet0_increasing ? facet0_lo_local_corner : facet0_hi_local_corner);
        const GEO::index_t facet1_lo_local_corner = source_mesh.facets.find_edge(facet1, lo_vertex, hi_vertex);
        const GEO::index_t facet1_hi_local_corner = source_mesh.facets.find_edge(facet1, hi_vertex, lo_vertex);
        const bool         facet1_increasing      = facet1_lo_local_corner != GEO::NO_INDEX;
        if (!facet1_increasing && (facet1_hi_local_corner == GEO::NO_INDEX)) {
            continue; // edge not found in facet1 - skip inconsistent edge
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
            // Degenerate - use average of adjacent face normals
            normal = GEO::normalize(
                mesh_facet_normalf(source_mesh, facet0) +
                mesh_facet_normalf(source_mesh, facet1)
            );
        }
        hex_planes[src_edge].normal = normal;
        hex_planes[src_edge].d = -GEO::dot(normal, center);
    }

    // Step 3: Create one destination vertex per source vertex, in source order, so
    // the destination vertex index equals the source vertex index (the hexagon /
    // bevel-quad construction in step 4 relies on this). make_new_dst_vertex_from_src_vertex
    // also populates m_vertex_src_to_dst (used by emit_unselected_facets_with_boundary_splice
    // and the component-selection remap) and registers a self-source; the self-source is
    // never used to move the vertex because this operation sets positions directly and
    // does not call interpolate_mesh_attributes().
    //
    // An interior vertex V is moved to V' on the ray through the corner-loop polygon
    // centroid along the polygon normal, least-squares fitted to the incident hex
    // planes. A boundary / exterior vertex is pinned at its original position.
    for (GEO::index_t src_vertex : source_mesh.vertices) {
        const GEO::index_t new_dst_vertex = make_new_dst_vertex_from_src_vertex(src_vertex);
        const GEO::vec3f V = get_pointf(source_mesh.vertices, src_vertex);

        if (!interior_selected(src_vertex)) {
            set_pointf(destination_mesh.vertices, new_dst_vertex, V); // pinned
            continue;
        }

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

        // Polygon normal: average of adjacent source face normals, oriented to point
        // outward (from centroid toward V)
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
        // minimize Sum(n_i . (centroid + t*normal) + d_i)^2
        // a_i = n_i . centroid + d_i  (signed distance from centroid to hex plane i)
        // b_i = n_i . normal          (projection of hex normal onto polygon normal)
        // t = -Sum(a_i * b_i) / Sum(b_i^2)
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
    }

    // Step 4: Shrunk face polygons - one per selected facet, plus a destination
    // vertex per corner at the inset position.
    std::vector<GEO::index_t> src_corner_to_dst_vertex;
    src_corner_to_dst_vertex.resize(source_mesh.facet_corners.nb());
    std::fill(src_corner_to_dst_vertex.begin(), src_corner_to_dst_vertex.end(), GEO::NO_INDEX);
    for (GEO::index_t src_facet : source_mesh.facets) {
        if (!is_facet_selected(src_facet)) {
            continue;
        }
        const GEO::index_t corner_count  = source_mesh.facets.nb_corners(src_facet);
        const GEO::index_t new_dst_facet = make_new_dst_facet_from_src_facet(src_facet, corner_count);
        for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
            const GEO::index_t corner            = source_mesh.facets.corner(src_facet, lc);
            const GEO::index_t dst_corner_vertex = destination_mesh.vertices.create_vertex();
            src_corner_to_dst_vertex[corner] = dst_corner_vertex;
            destination_mesh.facets.set_vertex(new_dst_facet, lc, dst_corner_vertex);
            set_pointf(destination_mesh.vertices, dst_corner_vertex, corner_inset_positions[corner]);
        }
    }

    // Step 5: Edge faces.
    //   - interior edge (both facets selected): hexagonal face, descending from both.
    //   - boundary edge (exactly one facet selected): bevel quad filling the gap
    //     between the shrunk selected face and the unchanged neighbor edge.
    //   - exterior edge (neither selected) / non-manifold edge: nothing.
    for (GEO::index_t src_edge : source_mesh.edges) {
        const std::vector<GEO::index_t>& edge_facets = source.get_edge_facets(src_edge);
        if (edge_facets.size() != 2) {
            continue;
        }
        const GEO::index_t facet0 = edge_facets[0];
        const GEO::index_t facet1 = edge_facets[1];
        const bool         sel0   = is_facet_selected(facet0);
        const bool         sel1   = is_facet_selected(facet1);
        if (!sel0 && !sel1) {
            continue; // Exterior edge: handled by emit_unselected_facets_with_boundary_splice.
        }

        const GEO::index_t lo_vertex = source_mesh.edges.vertex(src_edge, 0);
        const GEO::index_t hi_vertex = source_mesh.edges.vertex(src_edge, 1);

        if (sel0 && sel1) {
            // Interior edge -> hexagonal face (unchanged whole-mesh construction).
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
            add_facet_source(new_dst_facet, 1.0f, facet0);
            add_facet_source(new_dst_facet, 1.0f, facet1);
        } else {
            // Boundary edge -> bevel quad. Build it from the SELECTED facet's directed
            // edge v -> w (the corner find_edge returns is always the start of that
            // directed edge), so the winding is uniform regardless of which of
            // facet0 / facet1 is selected:
            //   [ dst(v), dst(w), inset@w, inset@v ]
            // dst(v) -> dst(w) twins the 1:1 re-emitted unselected neighbor's w -> v;
            // inset@w -> inset@v twins the shrunk face's inset edge; the two spokes
            // twin the adjacent wedge faces around v and w.
            const GEO::index_t S        = sel0 ? facet0 : facet1;
            const GEO::index_t lo_local = source_mesh.facets.find_edge(S, lo_vertex, hi_vertex);
            const GEO::index_t hi_local = source_mesh.facets.find_edge(S, hi_vertex, lo_vertex);
            const bool         increasing = (lo_local != GEO::NO_INDEX);
            if (!increasing && (hi_local == GEO::NO_INDEX)) {
                continue; // edge not found in selected facet - skip inconsistent edge
            }
            const GEO::index_t S_corner      = source_mesh.facets.corner(S, increasing ? lo_local : hi_local);
            const GEO::index_t S_next_corner = source_mesh.facets.next_corner_around_facet(S, S_corner);
            const GEO::index_t v             = source_mesh.facet_corners.vertex(S_corner);      // dst index == src index
            const GEO::index_t w             = source_mesh.facet_corners.vertex(S_next_corner); // dst index == src index
            const GEO::index_t inset_v       = src_corner_to_dst_vertex[S_corner];
            const GEO::index_t inset_w       = src_corner_to_dst_vertex[S_next_corner];
            if ((inset_v == GEO::NO_INDEX) || (inset_w == GEO::NO_INDEX)) {
                continue;
            }
            assert_elements_are_unique({v, w, inset_w, inset_v});
            const GEO::index_t new_dst_facet = destination_mesh.facets.create_polygon(4);
            destination_mesh.facets.set_vertex(new_dst_facet, 0, v);
            destination_mesh.facets.set_vertex(new_dst_facet, 1, w);
            destination_mesh.facets.set_vertex(new_dst_facet, 2, inset_w);
            destination_mesh.facets.set_vertex(new_dst_facet, 3, inset_v);
            add_facet_source(new_dst_facet, 1.0f, S);
        }
    }

    // Step 6: Re-emit the unselected facets. Chamfer inserts no edge midpoints, so
    // m_src_edge_to_dst_vertex is empty and this copies each unselected facet 1:1,
    // referencing the pinned m_vertex_src_to_dst vertices -> the seam welds with no
    // cracks and no T-junctions. No-op when there is no active selection.
    emit_unselected_facets_with_boundary_splice();

    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    if (m_selected_facets == nullptr) {
        destination.process({.flags = flags});
    } else {
        // Sanitize before process() to remove any degenerate facets (a valid chamfer
        // produces none, so this is a no-op there) before Geogram's connect().
        destination.sanitize();
        destination.process({.flags = flags});
    }
}

void chamfer3(const Geometry& source, Geometry& destination, float bevel_ratio, const std::set<GEO::index_t>* selected_facets, Component_remap* remap)
{
    Chamfer3 operation{source, destination, bevel_ratio, selected_facets};
    operation.build();
    if ((remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr)) {
        operation.remap_component_selection(*remap->source, *remap->destination);
    }
}

} // namespace erhe::geometry::operation
