#include "erhe_geometry/operation/conway/chamfer_old.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/plane_intersection.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::geometry::operation {

using erhe::geometry::Planef;
using erhe::geometry::intersect_three_planes_least_squares;

class Chamfer : public Geometry_operation
{
public:
    Chamfer(const Geometry& source, Geometry& destination);

    void build();
};

Chamfer::Chamfer(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

// https://en.wikipedia.org/wiki/Plane%E2%80%93plane_intersectio
//  - n1, h1 = normal of plane 1 (a, b, c), distance of plane 1 (d)
//  - n2, h2 = normal of plane 2 (a, b, c), distance of plane 2 (d)
//  - r      = any point on plane (x, y, z)
//  - lambda = t parameter for ray
[[nodiscard]] auto intersect_two_planes(const Planef& p1, const Planef& p2, GEO::vec3f& out_origin, GEO::vec3f& out_direction) -> bool
{
    const float dot = GEO::dot(p1.normal(), p2.normal());
    if (std::abs(dot) >= 1.0 - 1e-06f) {
        return false;
    }

    const float inv_det = 1.0f / (1.0f - dot * dot);
    const float c0 = (-p1.d + dot * p2.d) * inv_det;
    const float c1 = (-p2.d + dot * p1.d) * inv_det;

    out_origin = c0 * p1.normal() + c1 * p2.normal();
    out_direction = GEO::cross(p1.normal(), p2.normal());
    out_direction = GEO::normalize(out_direction);
    return true;
}

[[nodiscard]] auto get_signed_distance(const Planef& plane, const GEO::vec3f point) -> float
{
    const float distance = -plane.d;
    return GEO::dot(plane.normal(), point) - distance;
}

[[nodiscard]] auto intersect_plane(const Planef& plane, const GEO::vec3f& ray_origin, const GEO::vec3f& ray_direction) -> std::optional<float>
{
    const GEO::vec3f plane_normal = plane.normal();
    const float denominator = GEO::dot(ray_direction, plane_normal);
    if (std::abs(denominator) <= 1e-6f) {
        if (std::abs(get_signed_distance(plane, ray_origin)) <= 1e-6f) {
            return 0.0f;
        } else {
            return {};
        }
    }

    return -(GEO::dot(ray_origin, plane_normal) + plane.d) / denominator;
}

[[nodiscard]] auto closest_points_on_two_lines(
    const GEO::vec3f& P0,
    const GEO::vec3f& P1,
    const GEO::vec3f& Q0,
    const GEO::vec3f& Q1
) -> std::optional<std::pair<GEO::vec3f, GEO::vec3f>>
{
    const GEO::vec3f u  = P1 - P0;
    const GEO::vec3f v  = Q1 - Q0;
    const GEO::vec3f w0 = P0 - Q0;
    const float      a  = GEO::dot(u, u);
    const float      b  = GEO::dot(u, v);
    const float      c  = GEO::dot(v, v);
    const float      d  = GEO::dot(u, w0);
    const float      e  = GEO::dot(v, w0);
    const float      denominator = (a * c) - (b * b);

    if (denominator < std::numeric_limits<float>::epsilon()) {
        return {};
    }

    const auto sC = ((b * e) - (c * d)) / denominator;
    const auto tC = ((a * e) - (b * d)) / denominator;

    return std::pair<GEO::vec3f, GEO::vec3f>{ P0 + sC * u, Q0 + tC * v };
}

[[nodiscard]] auto closest_point_on_line(const GEO::vec3f& P0, const GEO::vec3f& P1, const GEO::vec3f& Q) -> std::optional<GEO::vec3f>
{
    const GEO::vec3f u = P1 - P0;
    if (dot(u, u) < std::numeric_limits<float>::epsilon()) {
        return {};
    }
    const float t = GEO::dot(u, Q - P0) / dot(u, u);
    return P0 + t * u;
}


auto line_point_distance(const GEO::vec3f& P0, const GEO::vec3f& P1, const GEO::vec3f& Q) -> std::optional<float>
{
    std::optional<GEO::vec3f> PC = closest_point_on_line(P0, P1, Q);
    if (!PC.has_value()) {
        return {};
    }
    return GEO::distance(Q, PC.value());
}

constexpr float coplanar_blend_lo    = 0.80f;
constexpr float coplanar_blend_hi    = 0.98f;
constexpr float coplanar_inset_ratio = 0.15f;

[[nodiscard]] auto smoothstep(float edge0, float edge1, float x) -> float
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - (2.0f * t));
}

// Non-coplanar path: intersect edge plane with face plane. Returns false if degenerate.
auto compute_edgeplane_inset_line(
    const Planef&     edge_plane,
    const GEO::vec3f& face_normal,
    const GEO::vec3f& face_centroid,
    GEO::vec3f&       out_point,
    GEO::vec3f&       out_direction
) -> bool
{
    const GEO::vec3f cross = GEO::cross(edge_plane.normal(), face_normal);
    const float cross_len = GEO::length(cross);
    if (cross_len <= 0.1f) { return false; }
    out_direction = cross / cross_len;
    const float face_d = -GEO::dot(face_normal, face_centroid);
    const float edge_d = edge_plane.d;
    const GEO::vec3f n1 = edge_plane.normal();
    const GEO::vec3f n2 = face_normal;
    const float denom = GEO::dot(out_direction, GEO::cross(n1, n2));
    if (std::abs(denom) <= 1e-7f) { return false; }
    out_point = ((-edge_d) * GEO::cross(n2, out_direction) +
                 (-face_d) * GEO::cross(out_direction, n1)) / denom;
    return true;
}

// Intersect two lines in 3D (assumed coplanar). Returns false if parallel.
auto intersect_two_lines(
    const GEO::vec3f& p1, const GEO::vec3f& d1,
    const GEO::vec3f& p2, const GEO::vec3f& d2,
    GEO::vec3f& out_point
) -> bool
{
    const GEO::vec3f cross_dir = GEO::cross(d1, d2);
    const float cross_sq = GEO::dot(cross_dir, cross_dir);
    if (cross_sq <= 1e-12f) { return false; }
    const float t = GEO::dot(GEO::cross(p2 - p1, d2), cross_dir) / cross_sq;
    out_point = p1 + t * d1;
    return true;
}

// Collect weighted constraint lines for one edge at a face corner.
// The face-local constraint is weighted by edge_blend (high for coplanar).
// The edge-plane constraint is weighted by (1 - edge_blend) (high for non-coplanar).
// Returns the number of lines added (1 or 2).
auto collect_edge_constraint_lines(
    const Planef&     edge_plane,
    const GEO::vec3f& face_normal,
    const GEO::vec3f& face_centroid,
    const GEO::vec3f& edge_p0,
    const GEO::vec3f& edge_p1,
    float             /*blend*/,
    GEO::vec3f*       out_points,     // array of at least 2
    GEO::vec3f*       out_directions, // array of at least 2
    float*            out_weights     // array of at least 2
) -> int
{
    int count = 0;

    // Face-local constraint: offset the edge inward within the face plane.
    // The offset is derived from the edge plane's height-based offset,
    // projected to the face plane, so it matches the edge-plane constraint
    // position for non-coplanar edges. For coplanar edges (where the
    // projection diverges), cap at coplanar_inset_ratio * edge_length.
    const GEO::vec3f edge_dir = edge_p1 - edge_p0;
    const float edge_len = GEO::length(edge_dir);
    GEO::vec3f inward = GEO::cross(face_normal, edge_dir);
    if (edge_len > 1e-7f) { inward = inward / edge_len; }
    if (GEO::dot(inward, face_centroid - 0.5f * (edge_p0 + edge_p1)) < 0.0f) {
        inward = -inward;
    }

    // Project edge plane offset to face plane: offset / sin(angle between
    // edge plane normal and face normal). Capped for near-coplanar edges.
    const float edge_face_dot = std::abs(GEO::dot(edge_plane.normal(), face_normal));
    const float sin_angle = std::sqrt(std::max(0.0f, 1.0f - (edge_face_dot * edge_face_dot)));
    const float plane_offset = std::abs(GEO::dot(edge_plane.normal(), edge_p0) + edge_plane.d);
    float face_offset;
    if (sin_angle > 0.15f) {
        face_offset = std::min(plane_offset / sin_angle, 0.4f * edge_len);
    } else {
        face_offset = coplanar_inset_ratio * edge_len;
    }

    out_points[count]     = edge_p0 + face_offset * inward;
    out_directions[count] = edge_dir;
    out_weights[count]    = 1.0f;
    ++count;

    // Edge-plane constraint: weight scales inversely with coplanarity
    const GEO::vec3f cross = GEO::cross(edge_plane.normal(), face_normal);
    const float cross_len = GEO::length(cross);
    if (cross_len > 0.1f) {
        const GEO::vec3f dir = cross / cross_len;
        const float face_d  = -GEO::dot(face_normal, face_centroid);
        const float edge_d  = edge_plane.d;
        const GEO::vec3f n1 = edge_plane.normal();
        const GEO::vec3f n2 = face_normal;
        const float denom = GEO::dot(dir, GEO::cross(n1, n2));
        if (std::abs(denom) > 1e-7f) {
            out_points[count]     = ((-edge_d) * GEO::cross(n2, dir) +
                                     (-face_d) * GEO::cross(dir, n1)) / denom;
            out_directions[count] = dir;
            out_weights[count]    = 1.0f;
            ++count;
        }
    }

    return count;
}

// Find the point that minimizes total squared distance to a set of lines,
// with Tikhonov regularization pulling toward reference_point.
//
// For each line (pi, di), the squared distance from point x to the line is:
//   ||(x - pi) - ((x - pi) . di) * di||^2 = (x - pi)^T (I - di di^T) (x - pi)
//
// Summing over all lines and differentiating:
//   M x = c   where M = Σ (I - di di^T),  c = Σ (I - di di^T) pi
//
// Add regularization: (M + eps*I) x = c + eps * reference_point
auto least_squares_closest_point_to_lines(
    const GEO::vec3f* points,
    const GEO::vec3f* directions,
    const float*      weights,
    int               line_count,
    const GEO::vec3f& reference_point
) -> GEO::vec3f
{
    constexpr float epsilon = 1e-4f;

    // Build M = Σ wi*(I - di*di^T) + eps*I  and  c = Σ wi*(I - di*di^T)*pi + eps*ref
    float m00 = epsilon, m01 = 0.0f, m02 = 0.0f;
    float                m11 = epsilon, m12 = 0.0f;
    float                               m22 = epsilon;
    float r0 = epsilon * reference_point.x;
    float r1 = epsilon * reference_point.y;
    float r2 = epsilon * reference_point.z;

    for (int i = 0; i < line_count; ++i) {
        const GEO::vec3f d = GEO::normalize(directions[i]);
        const GEO::vec3f p = points[i];
        const float      w = weights[i];

        // N = w * (I - d*d^T)  (weighted projection matrix)
        const float n00 = w * (1.0f - (d.x * d.x));
        const float n01 = w * (     - (d.x * d.y));
        const float n02 = w * (     - (d.x * d.z));
        const float n11 = w * (1.0f - (d.y * d.y));
        const float n12 = w * (     - (d.y * d.z));
        const float n22 = w * (1.0f - (d.z * d.z));

        m00 += n00; m01 += n01; m02 += n02;
                    m11 += n11; m12 += n12;
                                m22 += n22;

        // w * N * p
        r0 += (n00 * p.x) + (n01 * p.y) + (n02 * p.z);
        r1 += (n01 * p.x) + (n11 * p.y) + (n12 * p.z);
        r2 += (n02 * p.x) + (n12 * p.y) + (n22 * p.z);
    }

    // Solve via Cholesky: M = L * L^T
    const float l00 = std::sqrt(m00);
    const float l10 = m01 / l00;
    const float l11 = std::sqrt(m11 - (l10 * l10));
    const float l20 = m02 / l00;
    const float l21 = (m12 - (l20 * l10)) / l11;
    const float l22 = std::sqrt(m22 - (l20 * l20) - (l21 * l21));

    // Forward substitution: L y = r
    const float y0 = r0 / l00;
    const float y1 = (r1 - (l10 * y0)) / l11;
    const float y2 = (r2 - (l20 * y0) - (l21 * y1)) / l22;

    // Back substitution: L^T x = y
    const float x2 = y2 / l22;
    const float x1 = (y1 - (l21 * x2)) / l11;
    const float x0 = (y0 - (l10 * x1) - (l20 * x2)) / l00;

    return GEO::vec3f{x0, x1, x2};
}

void assert_elements_are_unique(std::initializer_list<GEO::index_t> v)
{
    for (GEO::index_t index : v) {
        ERHE_VERIFY(index != GEO::NO_INDEX);
    }
    std::set<GEO::index_t> set{v};
    ERHE_VERIFY(set.size() == v.size());
}

void Chamfer::build()
{
    // TODO: This operation bypasses the Geometry_operation attribute interpolation
    //       infrastructure. All vertex/corner/facet attributes (normals, texcoords,
    //       colors, tangents, joint weights) are lost. Fix by registering attribute
    //       sources via add_vertex_source/add_corner_source/add_facet_source for
    //       every created element, then calling post_processing() instead of the
    //       manual process() call at the end.
    // TODO: Remove commented-out debug visualization code (~200 lines).

    //  https://en.wikipedia.org/wiki/File:Modell,_Kristallform_Oktaeder-Rhombendodekaeder_-Krantz_432-.jpg
    // Chamfer
    // - All src vertices are copied as is to dst mesh
    // - Create new vertices to each corner of each face
    // - For each src edge, compute edge plane
    //      - using edge midpoint as point on plane
    //      - using average of facets normals

    // Pass 1: Compute edge planes and coplanarity blend weights
    GEO::vector<Planef> edge_planes;
    GEO::vector<float>  edge_blend;
    edge_planes.resize(source_mesh.edges.nb());
    edge_blend.resize(source_mesh.edges.nb());
    float min_edge_length = std::numeric_limits<float>::max();
    GEO::vector<float> vertex_min_heights;
    vertex_min_heights.resize(source_mesh.vertices.nb());
    std::fill(vertex_min_heights.begin(), vertex_min_heights.end(), std::numeric_limits<float>::max());
    float min_height = std::numeric_limits<float>::max();
    for (GEO::index_t src_edge : source_mesh.edges) {
        const std::vector<GEO::index_t>& edge_facets = source.get_edge_facets(src_edge);
        GEO::vec3f normal_sum{0.0f, 0.0f, 0.0f};
        for (GEO::index_t facet : edge_facets) {
            normal_sum += mesh_facet_normalf(source_mesh, facet);
        }
        const GEO::vec3f   normal   = GEO::normalize(normal_sum);
        const GEO::index_t v0       = source_mesh.edges.vertex(src_edge, 0);
        const GEO::index_t v1       = source_mesh.edges.vertex(src_edge, 1);
        const GEO::vec3f   p0       = get_pointf(source_mesh.vertices, v0);
        const GEO::vec3f   p1       = get_pointf(source_mesh.vertices, v1);
        const GEO::vec3f   midpoint = 0.5f * (p0 + p1);
        const float        length   = GEO::distance(p0, p1);
        edge_planes[src_edge] = Planef{midpoint, normal};
        min_edge_length = std::min(min_edge_length, length);
        if (edge_facets.size() == 2) {
            const GEO::vec3f n0 = GEO::normalize(mesh_facet_normalf(source_mesh, edge_facets[0]));
            const GEO::vec3f n1 = GEO::normalize(mesh_facet_normalf(source_mesh, edge_facets[1]));
            edge_blend[src_edge] = smoothstep(coplanar_blend_lo, coplanar_blend_hi, GEO::dot(n0, n1));
        } else {
            edge_blend[src_edge] = 0.0f;
        }
        if (edge_facets.size() == 2) {
            const GEO::index_t lhs_facet    = edge_facets[0];
            const GEO::index_t rhs_facet    = edge_facets[1];
            const GEO::vec3f   lhs_centroid = mesh_facet_centerf(source_mesh, lhs_facet);
            const GEO::vec3f   rhs_centroid = mesh_facet_centerf(source_mesh, rhs_facet);
            const std::optional<float> edge_height = line_point_distance(lhs_centroid, rhs_centroid, midpoint);
            if (edge_height.has_value()) {
                const float height = edge_height.value();
                vertex_min_heights[v0] = std::min(vertex_min_heights[v0], height);
                vertex_min_heights[v1] = std::min(vertex_min_heights[v1], height);
                min_height = std::min(min_height, height);
            }
        }
    }

    // Per vertex min

    //const double plane_move_distance = min_edge_length / 20.0f;
    for (GEO::index_t src_edge : source_mesh.edges) {
        const GEO::index_t v0       = source_mesh.edges.vertex(src_edge, 0);
        const GEO::index_t v1       = source_mesh.edges.vertex(src_edge, 1);
        const GEO::vec3f   p0       = get_pointf(source_mesh.vertices, v0);
        const GEO::vec3f   p1       = get_pointf(source_mesh.vertices, v1);
        const GEO::vec3f   midpoint = 0.5f * (p0 + p1);
        const float        height   = std::min(vertex_min_heights[v0], vertex_min_heights[v1]);
        Planef& plane = edge_planes[src_edge];
        plane.d += 0.5f * height;
        //const GEO::vec3 endpoint = midpoint + 0.1 * plane.normal();
        // Visual verification:
        // source.add_debug_line(GEO::NO_INDEX, GEO::NO_INDEX, to_glm_vec3(midpoint), to_glm_vec3(endpoint), glm::vec4{1.0f, 0.5f, 1.0f, 1.0f}, 1.0f);
        // source.add_debug_text(GEO::NO_INDEX, GEO::NO_INDEX, to_glm_vec3(endpoint), 0xffffffff, fmt::format("{}", src_edge));
    }

    // Pass 2: Precompute ALL corner inset positions using the decomposed
    // two-line intersection approach, and accumulate them per source vertex
    // for the inset vertex positions.
    //
    // For each face corner, we intersect each adjacent edge plane with the
    // face plane to get an inset line (with face-local fallback for coplanar
    // edges), then intersect the two inset lines. This is mathematically
    // equivalent to the three-plane intersection for well-conditioned cases
    // but handles degenerate (coplanar) cases gracefully.
    //
    // The inset vertex position = average of corner positions from all faces
    // at the vertex. This replaces the old edge-plane-intersection approach
    // which was ill-conditioned for coplanar adjacent faces.
    std::vector<GEO::vec3f> corner_inset_positions;
    corner_inset_positions.resize(source_mesh.facet_corners.nb());


    for (GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t corner_count  = source_mesh.facets.nb_corners(src_facet);
        const GEO::vec3f   facet_normal  = GEO::normalize(mesh_facet_normalf(source_mesh, src_facet));
        const GEO::vec3f   facet_centroid = mesh_facet_centerf(source_mesh, src_facet);

        for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
            const GEO::index_t prev_lc = (lc + corner_count - 1) % corner_count;
            const GEO::index_t next_lc = (lc + 1) % corner_count;
            const GEO::index_t corner      = source_mesh.facets.corner(src_facet, lc);
            const GEO::index_t prev_corner = source_mesh.facets.corner(src_facet, prev_lc);
            const GEO::index_t next_corner = source_mesh.facets.corner(src_facet, next_lc);
            const GEO::index_t vertex      = source_mesh.facet_corners.vertex(corner);
            const GEO::index_t prev_vertex = source_mesh.facet_corners.vertex(prev_corner);
            const GEO::index_t next_vertex = source_mesh.facet_corners.vertex(next_corner);
            const GEO::index_t lhs_edge    = source.get_edge(prev_vertex, vertex);
            const GEO::index_t rhs_edge    = source.get_edge(vertex, next_vertex);
            if ((lhs_edge == GEO::NO_EDGE) || (rhs_edge == GEO::NO_EDGE)) {
                corner_inset_positions[corner] = get_pointf(source_mesh.vertices, vertex);
                continue;
            }

            // Project adjacent vertices onto face plane
            const GEO::vec3f p_prev = get_pointf(source_mesh.vertices, prev_vertex);
            const GEO::vec3f p_curr = get_pointf(source_mesh.vertices, vertex);
            const GEO::vec3f p_next = get_pointf(source_mesh.vertices, next_vertex);
            const GEO::vec3f pp_prev = p_prev - GEO::dot(p_prev - facet_centroid, facet_normal) * facet_normal;
            const GEO::vec3f pp_curr = p_curr - GEO::dot(p_curr - facet_centroid, facet_normal) * facet_normal;
            const GEO::vec3f pp_next = p_next - GEO::dot(p_next - facet_centroid, facet_normal) * facet_normal;

            // Always use least-squares: collect all constraint lines from
            // both edges with equal weight. No coplanarity thresholds.
            GEO::vec3f line_points[4];
            GEO::vec3f line_dirs[4];
            float      line_weights[4];
            int line_count = 0;

            const int lhs_count = collect_edge_constraint_lines(
                edge_planes[lhs_edge], facet_normal, facet_centroid,
                pp_prev, pp_curr, 0.0f,
                line_points + line_count, line_dirs + line_count, line_weights + line_count
            );
            // Override weights to 1.0
            for (int i = 0; i < lhs_count; ++i) { line_weights[line_count + i] = 1.0f; }
            line_count += lhs_count;

            const int rhs_count = collect_edge_constraint_lines(
                edge_planes[rhs_edge], facet_normal, facet_centroid,
                pp_curr, pp_next, 0.0f,
                line_points + line_count, line_dirs + line_count, line_weights + line_count
            );
            for (int i = 0; i < rhs_count; ++i) { line_weights[line_count + i] = 1.0f; }
            line_count += rhs_count;

            GEO::vec3f inset_pos = least_squares_closest_point_to_lines(
                line_points, line_dirs, line_weights, line_count, pp_curr
            );

            // Clamp to 60% of centroid distance
            const GEO::vec3f to_c = facet_centroid - pp_curr;
            const GEO::vec3f to_i = inset_pos - pp_curr;
            const float c_sq = GEO::dot(to_c, to_c);
            const float i_sq = GEO::dot(to_i, to_i);
            if ((c_sq > 1e-12f) && (i_sq > (0.36f * c_sq))) {
                inset_pos = pp_curr + (0.6f * std::sqrt(c_sq / i_sq)) * to_i;
            }

            corner_inset_positions[corner] = inset_pos;
        }
    }

    // Inset vertex positions: least-squares fitting to ALL edge planes at
    // each vertex. Each edge plane provides a constraint "the vertex should
    // be on this plane." The LS solver minimizes total squared distance to
    // all planes with Tikhonov regularization toward the original vertex.
    // No thresholds, no fallback switching - works uniformly for coplanar
    // and non-coplanar edges.
    for (GEO::index_t src_vertex : source_mesh.vertices) {
        const GEO::index_t new_dst_vertex = destination_mesh.vertices.create_vertex();
        const GEO::vec3f original_pos = get_pointf(source_mesh.vertices, src_vertex);
        const std::vector<GEO::index_t>& v_edges = source.get_vertex_edges(src_vertex);

        if (v_edges.empty()) {
            set_pointf(destination_mesh.vertices, new_dst_vertex, original_pos);
            continue;
        }

        // Build LS system: minimize Σ (n_i . x + d_i)^2 + eps * ||x - ref||^2
        constexpr float epsilon = 1e-4f;
        float m00 = epsilon, m01 = 0.0f, m02 = 0.0f;
        float                m11 = epsilon, m12 = 0.0f;
        float                               m22 = epsilon;
        float r0 = epsilon * original_pos.x;
        float r1 = epsilon * original_pos.y;
        float r2 = epsilon * original_pos.z;

        for (GEO::index_t e : v_edges) {
            const Planef& plane = edge_planes[e];
            const GEO::vec3f n = plane.normal();

            m00 += n.x * n.x; m01 += n.x * n.y; m02 += n.x * n.z;
                               m11 += n.y * n.y; m12 += n.y * n.z;
                                                  m22 += n.z * n.z;
            r0 += (-plane.d) * n.x;
            r1 += (-plane.d) * n.y;
            r2 += (-plane.d) * n.z;
        }

        // Cholesky solve: M = L L^T
        const float l00 = std::sqrt(m00);
        const float l10 = m01 / l00;
        const float l11 = std::sqrt(m11 - (l10 * l10));
        const float l20 = m02 / l00;
        const float l21 = (m12 - (l20 * l10)) / l11;
        const float l22 = std::sqrt(m22 - (l20 * l20) - (l21 * l21));

        const float y0 = r0 / l00;
        const float y1 = (r1 - (l10 * y0)) / l11;
        const float y2 = (r2 - (l20 * y0) - (l21 * y1)) / l22;

        const float x2 = y2 / l22;
        const float x1 = (y1 - (l21 * x2)) / l11;
        const float x0 = (y0 - (l10 * x1) - (l20 * x2)) / l00;

        GEO::vec3f inset_pos{x0, x1, x2};

        // Compute the corner-derived target: average of per-edge corner
        // midpoints. This represents the ideal depth (consistent with
        // corners) but may not lie on the edge planes.
        GEO::vec3f target_sum{0.0f, 0.0f, 0.0f};
        GEO::index_t target_count = 0;
        for (GEO::index_t e : v_edges) {
            const std::vector<GEO::index_t>& ef = source.get_edge_facets(e);
            if (ef.size() != 2) {
                continue;
            }
            GEO::vec3f corner_pos[2];
            int found = 0;
            for (GEO::index_t fi = 0; (fi < 2) && (found < 2); ++fi) {
                const GEO::index_t facet = ef[fi];
                const GEO::index_t cc = source_mesh.facets.nb_corners(facet);
                for (GEO::index_t lc = 0; lc < cc; ++lc) {
                    const GEO::index_t corner = source_mesh.facets.corner(facet, lc);
                    if (source_mesh.facet_corners.vertex(corner) == src_vertex) {
                        corner_pos[found] = corner_inset_positions[corner];
                        ++found;
                        break;
                    }
                }
            }
            if (found == 2) {
                target_sum += 0.5f * (corner_pos[0] + corner_pos[1]);
                ++target_count;
            }
        }

        // Clamp the LS result to not exceed the corner-derived depth.
        // The LS gives the right direction (on edge planes), the corner
        // average gives the right depth (consistent with corners).
        if (target_count > 0) {
            const GEO::vec3f corner_avg = target_sum / static_cast<float>(target_count);
            const GEO::vec3f to_corner = corner_avg - original_pos;
            const float corner_dist = GEO::length(to_corner);
            if (corner_dist > 1e-7f) {
                const GEO::vec3f dir = to_corner / corner_dist;
                const float proj = GEO::dot(inset_pos - original_pos, dir);
                if (proj > corner_dist) {
                    const float scale = corner_dist / proj;
                    inset_pos = original_pos + scale * (inset_pos - original_pos);
                }
            }
        }

        set_pointf(destination_mesh.vertices, new_dst_vertex, inset_pos);
    }

    // Create destination faces using precomputed corner positions from Pass 2
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
    }

    // Create new dst hexagon facets matching source mesh edges
    for (GEO::index_t src_edge : source_mesh.edges) {
        const std::vector<GEO::index_t>& edge_facets = source.get_edge_facets(src_edge);
        if (edge_facets.size() != 2) {
            continue; // TODO
        }
        const GEO::index_t lo_vertex              = source_mesh.edges.vertex(src_edge, 0);
        const GEO::index_t hi_vertex              = source_mesh.edges.vertex(src_edge, 1);
        const GEO::index_t facet0                 = edge_facets[0];
        const GEO::index_t facet1                 = edge_facets[1];
        const GEO::index_t facet0_lo_local_corner = source_mesh.facets.find_edge(facet0, lo_vertex, hi_vertex);
        const GEO::index_t facet0_hi_local_corner = source_mesh.facets.find_edge(facet0, hi_vertex, lo_vertex);
        const bool         facet0_increasing      = facet0_lo_local_corner != GEO::NO_INDEX;
        const bool         facet0_decreasing      = facet0_hi_local_corner != GEO::NO_INDEX;
        const GEO::index_t facet0_corner          = source_mesh.facets.corner(facet0, facet0_increasing ? facet0_lo_local_corner : facet0_hi_local_corner);
        const GEO::index_t facet1_lo_local_corner = source_mesh.facets.find_edge(facet1, lo_vertex, hi_vertex);
        const GEO::index_t facet1_hi_local_corner = source_mesh.facets.find_edge(facet1, hi_vertex, lo_vertex);
        const bool         facet1_increasing      = facet1_lo_local_corner != GEO::NO_INDEX;
        const bool         facet1_decreasing      = facet1_hi_local_corner != GEO::NO_INDEX;
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
            //  Source mesh         Destination mesh  .
            //                                        .
            //       facet 0            -------       .
            //    \     <     /       /         \     .
            //    hi---------lo      hi - - - - lo    .
            //    /     >     \       \         /     .
            //       facet 1            -------       .
            //                                        .
            assert_elements_are_unique({lo_vertex, facet1_next_vertex, facet1_vertex, hi_vertex, facet0_next_vertex, facet0_vertex});
            destination_mesh.facets.set_vertex(new_dst_facet, 0, lo_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 1, facet1_next_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 2, facet1_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 3, hi_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 4, facet0_next_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 5, facet0_vertex);
        } else {
            assert_elements_are_unique({hi_vertex, facet1_next_vertex, facet1_vertex, lo_vertex, facet0_next_vertex, facet0_vertex});
            //  Source mesh         Destination mesh  .
            //                                        .
            //       facet 0            -------       .
            //    \     <     /       /         \     .
            //    lo---------hi      lo - - - - hi    .
            //    /     >     \       \         /     .
            //       facet 1            -------       .
            //                                        .
            destination_mesh.facets.set_vertex(new_dst_facet, 0, hi_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 1, facet1_next_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 2, facet1_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 3, lo_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 4, facet0_next_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 5, facet0_vertex);
        }
    }

    // post_processing();
    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    destination.process(flags);

}

void chamfer_old(const Geometry& source, Geometry& destination)
{
    Chamfer operation{source, destination};
    operation.build();
}

} // namespace erhe::geometry::operation


#if 0
    struct Debug_src_vertex_entry
    {
        GEO::index_t src_vertex;
        GEO::index_t dst_facet;
        std::vector<GEO::index_t> corners;
    };
    std::vector<Debug_src_vertex_entry> debug_src_vertex_entries;

    struct Debug_src_facet_entry
    {
        GEO::index_t src_facet;
        GEO::index_t dst_facet;
        std::vector<GEO::index_t> corners;
    };
    std::vector<Debug_src_facet_entry> debug_src_facet_entries;




    //for (const Debug_src_vertex_entry& entry : debug_src_vertex_entries) {
    //    const GEO::vec3 p_src_vertex         = source_mesh.vertices.point(entry.src_vertex);
    //    const GEO::vec3 p_dst_facet_centroid = GEO::Geom::mesh_facet_center(destination_mesh, entry.dst_facet);
    //    const GEO::vec3 p_dst_facet_normal   = GEO::normalize(GEO::Geom::mesh_facet_normal(destination_mesh, entry.dst_facet));
    //    const glm::vec3 C                    = to_glm_vec3(p_src_vertex);
    //    glm::vec3 prev = C;
    //    int local_corner = 0;
    //    for (GEO::index_t dst_corner_vertex : entry.corners) {
    //        const GEO::vec3 p_dst_corner_vertex = destination_mesh.vertices.point(dst_corner_vertex);
    //        const glm::vec3 V = to_glm_vec3(p_dst_corner_vertex);
    //        destination.add_debug_line(dst_corner_vertex, entry.dst_facet, prev, V, glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
    //        destination.add_debug_line(dst_corner_vertex, entry.dst_facet, C,    V, glm::vec4{0.0f, 0.5f, 0.5f, 0.5f}, 1.0f);
    //        destination.add_debug_text(dst_corner_vertex, entry.dst_facet, V, 0xffffffff, fmt::format("{}:{}", dst_corner_vertex, local_corner));
    //        source     .add_debug_line(entry.src_vertex,  GEO::NO_INDEX,   prev, V, glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
    //        source     .add_debug_line(entry.src_vertex,  GEO::NO_INDEX,   C,    V, glm::vec4{0.0f, 0.5f, 0.5f, 0.5f}, 1.0f);
    //        source     .add_debug_text(entry.src_vertex,  GEO::NO_INDEX,   V, 0xffffffff, fmt::format("{}:{}", entry.src_vertex, local_corner));
    //        prev = V;
    //        ++local_corner;
    //    }
    //}
    for (const Debug_src_facet_entry& entry : debug_src_facet_entries) {
        const GEO::vec3 p_src_facet_centroid = GEO::Geom::mesh_facet_center(source_mesh, entry.src_facet);
        const glm::vec3 C                    = to_glm_vec3(p_src_facet_centroid);
        glm::vec3 prev = C;
        int local_corner = 0;
        for (GEO::index_t dst_corner_vertex : entry.corners) {
            const GEO::vec3 p_dst_corner_vertex = destination_mesh.vertices.point(dst_corner_vertex);
            const glm::vec3 V = to_glm_vec3(p_dst_corner_vertex);
            //destination.add_debug_line(GEO::NO_INDEX, entry.dst_facet, prev, V, glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
            destination.add_debug_line(GEO::NO_INDEX, entry.dst_facet, C,    V, glm::vec4{0.0f, 0.5f, 0.5f, 0.5f}, 1.0f);
            destination.add_debug_text(GEO::NO_INDEX, entry.dst_facet, V, 0xffffffff, fmt::format("{}.{}", entry.dst_facet, local_corner));
            //source     .add_debug_line(GEO::NO_INDEX, entry.src_facet, prev, V, glm::vec4{0.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
            source     .add_debug_line(GEO::NO_INDEX, entry.src_facet, C,    V, glm::vec4{0.0f, 0.5f, 0.5f, 0.5f}, 1.0f);
            source     .add_debug_text(GEO::NO_INDEX, entry.src_facet, V, 0xffffffff, fmt::format("{}.{}", entry.src_facet, local_corner));
            prev = V;
            ++local_corner;
        }
    }

#endif
