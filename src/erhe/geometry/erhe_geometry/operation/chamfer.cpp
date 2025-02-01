#include "erhe_geometry/operation/Chamfer.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <geogram/mesh/mesh_geometry.h>

namespace erhe::geometry::operation {

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

[[nodiscard]] auto are_planes_coplanar(const GEO::Plane& p1, const GEO::Plane& p2) -> bool
{
    const double dot = GEO::dot(p1.normal(), p2.normal());
    return std::abs(dot) >= 1.0 - 1e-06;
}

[[nodiscard]] auto intersect_three_planes(const GEO::Plane& a, const GEO::Plane& b, const GEO::Plane& c) -> std::optional<GEO::vec3>
{
    const GEO::vec3 a_n   = a.normal();
    const GEO::vec3 b_n   = b.normal();
    const GEO::vec3 c_n   = c.normal();
    const GEO::vec3 bxc   = GEO::cross(b_n, c_n);
    const GEO::vec3 cxa   = GEO::cross(c_n, a_n);
    const GEO::vec3 axb   = GEO::cross(a_n, b_n);
    const double    dot_a = GEO::dot(bxc, a_n);
    const double    dot_b = GEO::dot(cxa, b_n);
    const double    dot_c = GEO::dot(axb, c_n);
    static_cast<void>(dot_a);
    static_cast<void>(dot_b);
    static_cast<void>(dot_c);
    if (std::abs(dot_c) < 1e-06) {
        return {};
    }
    return (-a.d * bxc - b.d * cxa - c.d * axb) / dot_c;
}

// https://en.wikipedia.org/wiki/Plane%E2%80%93plane_intersectio
//  - n1, h1 = normal of plane 1 (a, b, c), distance of plane 1 (d)
//  - n2, h2 = normal of plane 2 (a, b, c), distance of plane 2 (d)
//  - r      = any point on plane (x, y, z)
//  - lambda = t parameter for ray
[[nodiscard]] auto intersect_two_planes(const GEO::Plane& p1, const GEO::Plane& p2, GEO::vec3& out_origin, GEO::vec3& out_direction) -> bool
{
    const double dot = GEO::dot(p1.normal(), p2.normal());
    if (std::abs(dot) >= 1.0 - 1e-06) {
        return false;
    }

    const double inv_det = 1.0 / (1.0 - dot * dot);
    const double c0 = (-p1.d + dot * p2.d) * inv_det;
    const double c1 = (-p2.d + dot * p1.d) * inv_det;

    out_origin = c0 * p1.normal() + c1 * p2.normal();
    out_direction = GEO::cross(p1.normal(), p2.normal());
    out_direction = GEO::normalize(out_direction);
    return true;
}

[[nodiscard]] auto get_signed_distance(const GEO::Plane& plane, const GEO::vec3 point) -> double
{
    const double distance = -plane.d;
    return GEO::dot(plane.normal(), point) - distance;
}

[[nodiscard]] auto intersect_plane(const GEO::Plane& plane, const GEO::vec3& ray_origin, const GEO::vec3& ray_direction) -> std::optional<double>
{
    const GEO::vec3 plane_normal = plane.normal();
    const double denominator = GEO::dot(ray_direction, plane_normal);
    if (std::abs(denominator) <= 1e-6) {
        if (std::abs(get_signed_distance(plane, ray_origin)) <= 1e-6) {
            return 0.0;
        } else {
            return {};
        }
    }

    return -(GEO::dot(ray_origin, plane_normal) + plane.d) / denominator;
}

[[nodiscard]] auto closest_points_on_two_lines(
    const GEO::vec3& P0,
    const GEO::vec3& P1,
    const GEO::vec3& Q0,
    const GEO::vec3& Q1
) -> std::optional<std::pair<GEO::vec3, GEO::vec3>>
{
    const GEO::vec3 u  = P1 - P0;
    const GEO::vec3 v  = Q1 - Q0;
    const GEO::vec3 w0 = P0 - Q0;
    const double    a  = GEO::dot(u, u);
    const double    b  = GEO::dot(u, v);
    const double    c  = GEO::dot(v, v);
    const double    d  = GEO::dot(u, w0);
    const double    e  = GEO::dot(v, w0);
    const double    denominator = (a * c) - (b * b);

    if (denominator < std::numeric_limits<double>::epsilon()) {
        return {};
    }

    const auto sC = ((b * e) - (c * d)) / denominator;
    const auto tC = ((a * e) - (b * d)) / denominator;

    return std::pair<GEO::vec3, GEO::vec3>{ P0 + sC * u, Q0 + tC * v };
}

[[nodiscard]] auto closest_point_on_line(const GEO::vec3& P0, const GEO::vec3& P1, const GEO::vec3& Q) -> std::optional<GEO::vec3>
{
    const GEO::vec3 u = P1 - P0;
    if (dot(u, u) < std::numeric_limits<double>::epsilon()) {
        return {};
    }
    const double t = GEO::dot(u, Q - P0) / dot(u, u);
    return P0 + t * u;
}


auto line_point_distance(const GEO::vec3& P0, const GEO::vec3& P1, const GEO::vec3& Q) -> std::optional<double>
{
    std::optional<GEO::vec3> PC = closest_point_on_line(P0, P1, Q);
    if (!PC.has_value()) {
        return {};
    }
    return GEO::distance(Q, PC.value());
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
    //  https://en.wikipedia.org/wiki/File:Modell,_Kristallform_Oktaeder-Rhombendodekaeder_-Krantz_432-.jpg
    // Chamfer
    // - All src vertices are copied as is to dst mesh
    // - Create new vertices to each corner of each face
    // - For each src edge, compute edge plane
    //      - using edge midpoint as point on plane
    //      - using average of facets normals

    // Pass 1: Compute edge planes
    GEO::vector<GEO::Plane> edge_planes;
    edge_planes.resize(source_mesh.edges.nb());
    double min_edge_length = std::numeric_limits<double>::max();
    GEO::vector<double> vertex_min_heights;
    vertex_min_heights.resize(source_mesh.vertices.nb());
    std::fill(vertex_min_heights.begin(), vertex_min_heights.end(), std::numeric_limits<double>::max());
    double min_height = std::numeric_limits<double>::max();
    for (GEO::index_t src_edge : source_mesh.edges) {
        const std::vector<GEO::index_t>& edge_facets = source.get_edge_facets(src_edge);
        GEO::vec3 normal_sum{0.0, 0.0, 0.0};
        for (GEO::index_t facet : edge_facets) {
            normal_sum += GEO::Geom::mesh_facet_normal(source_mesh, facet);
        }
        const GEO::vec3    normal   = GEO::normalize(normal_sum);
        const GEO::index_t v0       = source_mesh.edges.vertex(src_edge, 0);
        const GEO::index_t v1       = source_mesh.edges.vertex(src_edge, 1);
        const GEO::vec3    p0       = source_mesh.vertices.point(v0);
        const GEO::vec3    p1       = source_mesh.vertices.point(v1);
        const GEO::vec3    midpoint = 0.5 * (p0 + p1);
        const double       length   = GEO::distance(p0, p1);
        edge_planes[src_edge] = GEO::Plane{midpoint, normal};
        min_edge_length = std::min(min_edge_length, length);
        if (edge_facets.size() == 2) {
            const GEO::index_t lhs_facet    = edge_facets[0];
            const GEO::index_t rhs_facet    = edge_facets[1];
            const GEO::vec3    lhs_centroid = GEO::Geom::mesh_facet_center(source_mesh, lhs_facet);
            const GEO::vec3    rhs_centroid = GEO::Geom::mesh_facet_center(source_mesh, rhs_facet);
            const std::optional<double> edge_height = line_point_distance(lhs_centroid, rhs_centroid, midpoint);
            if (edge_height.has_value()) {
                const double height = edge_height.value();
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
        const GEO::vec3    p0       = source_mesh.vertices.point(v0);
        const GEO::vec3    p1       = source_mesh.vertices.point(v1);
        const GEO::vec3    midpoint = 0.5 * (p0 + p1);
        const double       height   = std::min(vertex_min_heights[v0], vertex_min_heights[v1]);
        GEO::Plane& plane = edge_planes[src_edge];
        plane.d += 0.5 * height;
        //const GEO::vec3 endpoint = midpoint + 0.1 * plane.normal();
        // Visual verification:
        // source.add_debug_line(GEO::NO_INDEX, GEO::NO_INDEX, to_glm_vec3(midpoint), to_glm_vec3(endpoint), glm::vec4{1.0f, 0.5f, 1.0f, 1.0f}, 1.0f);
        // source.add_debug_text(GEO::NO_INDEX, GEO::NO_INDEX, to_glm_vec3(endpoint), 0xffffffff, fmt::format("{}", src_edge));
    }

    // Pass 2: Compute "inset" src vertex positions
    // - For every src facet
    //  - For every src corner of facet
    //    - Locate edges surrounding the corner
    //    - Compute parametric line from intersection planes for the surrounding edges
    //    - For all edges on the vertex, other than edges surrounding the facet corner,
    //      compute intersection of the parametric line and edge plane
    //    - Take average intersection point, accumulate to vertex position
    GEO::vector<GEO::vec3> vertex_position_sum;
    GEO::vector<bool> vertex_alternative_method;
    vertex_position_sum.resize(source_mesh.vertices.nb());
    vertex_alternative_method.resize(source_mesh.vertices.nb());
    std::fill(vertex_position_sum.begin(), vertex_position_sum.end(), GEO::vec3{0.0, 0.0, 0.0});
    std::fill(vertex_alternative_method.begin(), vertex_alternative_method.end(), false);
    for (GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t corner_count = source_mesh.facets.nb_corners(src_facet);
        for (GEO::index_t local_facet_corner = 0; local_facet_corner < corner_count; ++local_facet_corner) {
            const GEO::index_t prev_corner = source_mesh.facets.corner(src_facet, (local_facet_corner + corner_count - 1) % corner_count);
            const GEO::index_t corner      = source_mesh.facets.corner(src_facet,  local_facet_corner                                   );
            const GEO::index_t next_corner = source_mesh.facets.corner(src_facet, (local_facet_corner + 1               ) % corner_count);
            const GEO::index_t prev_vertex = source_mesh.facet_corners.vertex(prev_corner);
            const GEO::index_t vertex      = source_mesh.facet_corners.vertex(corner);
            const GEO::index_t next_vertex = source_mesh.facet_corners.vertex(next_corner);
            const GEO::index_t lhs_edge    = source.get_edge(prev_vertex, vertex);
            const GEO::index_t rhs_edge    = source.get_edge(vertex, next_vertex);
            if ((lhs_edge == GEO::NO_EDGE) || (rhs_edge == GEO::NO_EDGE)) {
                continue;
            }

            ERHE_VERIFY(lhs_edge != rhs_edge);
            const GEO::Plane& lhs_plane = edge_planes[lhs_edge];
            const GEO::Plane& rhs_plane = edge_planes[rhs_edge];
            GEO::vec3  origin   {0.0, 0.0, 0.0};
            GEO::vec3  direction{0.0, 0.0, 0.0};
            const bool line_ok = intersect_two_planes(lhs_plane, rhs_plane, origin, direction);
            if (!line_ok) {
                continue; // TODO
            }
            ERHE_VERIFY(line_ok); // TODO Handle case when planes are equal, for example
            double t_sum  {0.0};
            double t_count{0.0};

            //// {
            ////     const GEO::vec3 p0   = source_mesh.vertices.point(prev_vertex);
            ////     const GEO::vec3 p1   = source_mesh.vertices.point(vertex);
            ////     const GEO::vec3 p2   = source_mesh.vertices.point(next_vertex);
            ////     const GEO::vec3 mp01 = 0.5 * (p0 + p1);
            ////     const GEO::vec3 mp12 = 0.5 * (p1 + p2);
            ////     const GEO::vec3 ep01 = mp01 + 0.1 * lhs_plane.normal();
            ////     const GEO::vec3 ep12 = mp12 + 0.1 * rhs_plane.normal();
            ////     source.add_debug_line(GEO::NO_INDEX, src_facet, to_glm_vec3(mp01), to_glm_vec3(ep01), glm::vec4{1.0f, 2.0f, 0.0f, 1.0f}, 1.0f);
            ////     source.add_debug_line(GEO::NO_INDEX, src_facet, to_glm_vec3(mp12), to_glm_vec3(ep12), glm::vec4{1.0f, 2.0f, 0.0f, 1.0f}, 1.0f);
            //// }

            const std::vector<GEO::index_t>& vertex_edges = source.get_vertex_edges(vertex);
            for (GEO::index_t edge : vertex_edges) {
                if ((edge == lhs_edge) || (edge == rhs_edge)) {
                    continue;
                }
                const GEO::Plane& plane = edge_planes[edge];

                //// {
                ////     const GEO::vec3 p0   = source_mesh.vertices.point(source_mesh.edges.vertex(edge, 0));
                ////     const GEO::vec3 p1   = source_mesh.vertices.point(source_mesh.edges.vertex(edge, 1));
                ////     const GEO::vec3 mp01 = 0.5 * (p0 + p1);
                ////     const GEO::vec3 ep01 = mp01 + 0.1 * plane.normal();
                ////     source.add_debug_line(GEO::NO_INDEX, src_facet, to_glm_vec3(mp01), to_glm_vec3(ep01), glm::vec4{10.0f, 2.0f, 0.0f, 1.0f}, 1.0f);
                //// }

                //// {
                ////     std::optional<GEO::vec3> Q = intersect_three_planes(lhs_plane, rhs_plane, plane);
                ////     if (Q.has_value()) {
                ////         const GEO::vec3 P = source_mesh.vertices.point(vertex);
                ////         source.add_debug_line(vertex, src_facet, to_glm_vec3(P), to_glm_vec3(Q.value()), glm::vec4{20.0f, 0.0f, 20.0f, 1.0f}, 1.0f);
                ////         source.add_debug_text(vertex, src_facet, to_glm_vec3(P), 0xffffffff, fmt::format("{}", corner));
                ////     }
                //// }

                std::optional t = intersect_plane(plane, origin, direction);
                if (t.has_value()) {
                    t_sum += t.value();
                    t_count += 1.0;
                    //// {
                    ////     const GEO::vec3 intersection_position = origin + t.value() * direction;
                    ////     // Visual verification:
                    ////     const GEO::vec3 P = source_mesh.vertices.point(vertex);
                    ////     const GEO::vec3 Q = intersection_position;
                    ////     source.add_debug_line(vertex, src_facet, to_glm_vec3(P), to_glm_vec3(Q), glm::vec4{0.0f, 0.0f, 10.0f, 1.0f}, 1.0f);
                    ////     source.add_debug_text(vertex, src_facet, to_glm_vec3(P), 0xffffffff, fmt::format("{}", corner));
                    //// }
                }
            }
            if (t_count != 0.0) {
                const double t = t_sum / t_count;
                const GEO::vec3 vertex_position = origin + t * direction;
                vertex_position_sum[vertex] += vertex_position;
            } else {
                // TODO Can we do something better?
                vertex_alternative_method[vertex] = true;
            }

            // Visual verification:
            //const GEO::vec3 P = source_mesh.vertices.point(vertex);
            //const GEO::vec3 Q = vertex_position;
            //source.add_debug_line(vertex, src_facet, to_glm_vec3(P), to_glm_vec3(Q), glm::vec4{0.0f, 0.0f, 10.0f, 1.0f}, 1.0f);
            //source.add_debug_text(vertex, src_facet, to_glm_vec3(P), 0xffffffff, fmt::format("{}", corner));
        }

        //// const GEO::vec3 facet_normal   = GEO::normalize(GEO::Geom::mesh_facet_normal(source_mesh, src_facet));
        //// const GEO::vec3 facet_centroid = GEO::Geom::mesh_facet_center(source_mesh, src_facet);
        //// const GEO::vec3 P              = facet_centroid;
        //// const GEO::vec3 Q              = facet_centroid + 0.1 * facet_normal;
        //// source.add_debug_line(GEO::NO_INDEX, src_facet, to_glm_vec3(P), to_glm_vec3(Q), glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}, 1.0f);
    }

    // Create destination vertices matching source mesh vertices positions, with inset
    //make_dst_vertices_from_src_vertices();
    for (GEO::index_t src_vertex : source_mesh.vertices) {
        const GEO::index_t new_dst_vertex = destination_mesh.vertices.create_vertex();
        if (!vertex_alternative_method[src_vertex]) {
            const size_t vertex_corner_count = source.get_vertex_corners(src_vertex).size();
            ERHE_VERIFY(vertex_corner_count > 0);
            const GEO::vec3 new_pos = vertex_position_sum[src_vertex] / static_cast<double>(vertex_corner_count);
            destination_mesh.vertices.point(new_dst_vertex) = new_pos;

            //const GEO::vec3 p0 = source_mesh.vertices.point(src_vertex);
            //source.add_debug_line(src_vertex, GEO::NO_INDEX, to_glm_vec3(p0), to_glm_vec3(new_pos), glm::vec4{0.3f, 0.6f, 1.0f, 1.0f}, 1.0f);
            ////source.add_debug_text(src_vertex, GEO::NO_INDEX, to_glm_vec3(new_pos), 0xffffffff, fmt::format("{}", src_vertex));
            //destination.add_debug_line(new_dst_vertex, GEO::NO_INDEX, to_glm_vec3(p0), to_glm_vec3(new_pos), glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
            ////destination.add_debug_text(vertex, src_facet, to_glm_vec3(vertex_position), 0xffffffff, fmt::format("{}", vertex));
        } else {
            destination_mesh.vertices.point(new_dst_vertex) = source_mesh.vertices.point(src_vertex);
        }
    }

    // Create destination vertices matching source mesh facet corners, with inset
    // Create destination faces matching source mesh facets
    std::vector<GEO::index_t> src_corner_to_dst_vertex;
    src_corner_to_dst_vertex.resize(source_mesh.facet_corners.nb());
    std::fill(src_corner_to_dst_vertex.begin(), src_corner_to_dst_vertex.end(), GEO::NO_INDEX);
    for (GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t corner_count   = source_mesh.facets.nb_corners(src_facet);
        const GEO::index_t new_dst_facet  = destination_mesh.facets.create_polygon(corner_count);
        const GEO::vec3    facet_normal   = GEO::normalize(GEO::Geom::mesh_facet_normal(source_mesh, src_facet));
        const GEO::vec3    facet_centroid = GEO::Geom::mesh_facet_center(source_mesh, src_facet);
        const GEO::Plane   facet_plane{facet_centroid, facet_normal};
        for (GEO::index_t local_facet_corner = 0; local_facet_corner < corner_count; ++local_facet_corner) {
            const GEO::index_t prev_corner = source_mesh.facets.corner(src_facet, (local_facet_corner + corner_count - 1) % corner_count);
            const GEO::index_t corner      = source_mesh.facets.corner(src_facet,  local_facet_corner                                   );
            const GEO::index_t next_corner = source_mesh.facets.corner(src_facet, (local_facet_corner + 1               ) % corner_count);
            const GEO::index_t prev_vertex = source_mesh.facet_corners.vertex(prev_corner);
            const GEO::index_t vertex      = source_mesh.facet_corners.vertex(corner);
            const GEO::index_t next_vertex = source_mesh.facet_corners.vertex(next_corner);
            const GEO::index_t lhs_edge    = source.get_edge(prev_vertex, vertex);
            const GEO::index_t rhs_edge    = source.get_edge(vertex, next_vertex);
            if ((lhs_edge == GEO::NO_EDGE) || (rhs_edge == GEO::NO_EDGE)) {
                continue;
            }

            const GEO::index_t dst_corner_vertex = destination_mesh.vertices.create_vertex();
            src_corner_to_dst_vertex[corner] = dst_corner_vertex;
            destination_mesh.facets.set_vertex(new_dst_facet, local_facet_corner, dst_corner_vertex);

            const GEO::Plane& lhs_plane = edge_planes[lhs_edge];
            const GEO::Plane& rhs_plane = edge_planes[rhs_edge];
            std::optional<GEO::vec3> inset_facet_corner_point = intersect_three_planes(facet_plane, lhs_plane, rhs_plane);
            if (!inset_facet_corner_point.has_value()) {
                log_operation->warn("TODO coplanar chamfer is ");
                //GEO::vec3 fallback_point = 0.5 * (facet_centroid + source_mesh.vertices.point(vertex));
                const GEO::vec3 fallback_point = 0.5 * (facet_centroid + destination_mesh.vertices.point(vertex));
                destination_mesh.vertices.point(dst_corner_vertex) = fallback_point;
                static int counter = 0;
                ++counter;
            } else {
                ERHE_VERIFY(inset_facet_corner_point.has_value());
                const double magnitude = GEO::length(inset_facet_corner_point.value());
                if (magnitude > 100.0) {
                    static int counter = 0;
                    ++counter;
                }
                ERHE_VERIFY(magnitude < 100.0);
                destination_mesh.vertices.point(dst_corner_vertex) = inset_facet_corner_point.value();
            }
        }
    }

    // Create new dst hexagon facets matching source mesh edges
    for (GEO::index_t src_edge : source_mesh.edges) {
        const std::vector<GEO::index_t>& edge_facets = source.get_edge_facets(src_edge);
        ERHE_VERIFY(edge_facets.size() == 2); // TODO
        const GEO::index_t lo_vertex          = source_mesh.edges.vertex(src_edge, 0);
        const GEO::index_t hi_vertex          = source_mesh.edges.vertex(src_edge, 1);
        const GEO::index_t facet0             = edge_facets[0];
        const GEO::index_t facet1             = edge_facets[1];
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
            //  Source mesh         Destination mesh 
            //                                       
            //       facet 0            -------      
            //    \     <     /       /         \    
            //    hi---------lo      hi - - - - lo   
            //    /     >     \       \         /    
            //       facet 1            -------      
            //                                       
            assert_elements_are_unique({lo_vertex, facet1_next_vertex, facet1_vertex, hi_vertex, facet0_next_vertex, facet0_vertex});
            destination_mesh.facets.set_vertex(new_dst_facet, 0, lo_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 1, facet1_next_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 2, facet1_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 3, hi_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 4, facet0_next_vertex);
            destination_mesh.facets.set_vertex(new_dst_facet, 5, facet0_vertex);
        } else {
            assert_elements_are_unique({hi_vertex, facet1_next_vertex, facet1_vertex, lo_vertex, facet0_next_vertex, facet0_vertex});
            //  Source mesh         Destination mesh 
            //                                       
            //       facet 0            -------      
            //    \     <     /       /         \    
            //    lo---------hi      lo - - - - hi   
            //    /     >     \       \         /    
            //       facet 1            -------      
            //                                       
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

void chamfer(const Geometry& source, Geometry& destination)
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
