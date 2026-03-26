#include "erhe_geometry/self_intersection.hpp"

#include <geogram/basic/geometry.h>
#include <geogram/mesh/mesh.h>

#include <cmath>
#include <vector>

namespace erhe::geometry {

namespace {

class Triangle
{
public:
    GEO::vec3f   v[3];
    GEO::index_t vertex_indices[3];
    GEO::index_t facet_index;
};

auto count_shared_vertices(const Triangle& a, const Triangle& b) -> int
{
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (a.vertex_indices[i] == b.vertex_indices[j]) {
                ++count;
            }
        }
    }
    return count;
}

// Tests if segment (p0, p1) intersects triangle (v0, v1, v2) at an
// interior point of the segment (excluding endpoints within epsilon).
auto segment_intersects_triangle(
    const GEO::vec3f& p0,
    const GEO::vec3f& p1,
    const GEO::vec3f& v0,
    const GEO::vec3f& v1,
    const GEO::vec3f& v2
) -> bool
{
    constexpr float endpoint_epsilon = 1e-5f;
    constexpr float plane_epsilon    = 1e-7f;
    constexpr float bary_margin      = -1e-5f;

    const GEO::vec3f edge1  = v1 - v0;
    const GEO::vec3f edge2  = v2 - v0;
    const GEO::vec3f normal = GEO::cross(edge1, edge2);

    const float normal_len_sq = GEO::dot(normal, normal);
    if (normal_len_sq < 1e-12f) {
        return false; // degenerate triangle
    }

    // Signed distances from segment endpoints to triangle plane
    const float d0 = GEO::dot(normal, p0 - v0);
    const float d1 = GEO::dot(normal, p1 - v0);

    // Both on same side of plane
    if ((d0 * d1) > 0.0f) {
        return false;
    }

    // Both endpoints on the plane (coplanar - skip)
    if ((std::abs(d0) < plane_epsilon) && (std::abs(d1) < plane_epsilon)) {
        return false;
    }

    // Parameter where segment crosses the plane
    const float t = d0 / (d0 - d1);

    // Reject intersections too close to endpoints (shared vertex tolerance)
    if ((t < endpoint_epsilon) || (t > (1.0f - endpoint_epsilon))) {
        return false;
    }

    // Intersection point
    const GEO::vec3f point = p0 + t * (p1 - p0);

    // Barycentric coordinate test
    const GEO::vec3f vp    = point - v0;
    const float dot00 = GEO::dot(edge1, edge1);
    const float dot01 = GEO::dot(edge1, edge2);
    const float dot02 = GEO::dot(edge1, vp);
    const float dot11 = GEO::dot(edge2, edge2);
    const float dot12 = GEO::dot(edge2, vp);

    const float denom = (dot00 * dot11) - (dot01 * dot01);
    if (std::abs(denom) < 1e-12f) {
        return false; // degenerate triangle
    }

    const float inv_denom = 1.0f / denom;
    const float u = ((dot11 * dot02) - (dot01 * dot12)) * inv_denom;
    const float v = ((dot00 * dot12) - (dot01 * dot02)) * inv_denom;

    return (u >= bary_margin) && (v >= bary_margin) && ((u + v) <= (1.0f - bary_margin));
}

auto triangles_intersect(const Triangle& a, const Triangle& b) -> bool
{
    const int shared = count_shared_vertices(a, b);
    if (shared >= 2) {
        return false; // share an edge, adjacent faces naturally touch
    }

    // Test edges of A against triangle B
    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        if (segment_intersects_triangle(a.v[i], a.v[j], b.v[0], b.v[1], b.v[2])) {
            return true;
        }
    }

    // Test edges of B against triangle A
    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        if (segment_intersects_triangle(b.v[i], b.v[j], a.v[0], a.v[1], a.v[2])) {
            return true;
        }
    }

    return false;
}

auto get_vertex_position(const GEO::Mesh& mesh, GEO::index_t vertex) -> GEO::vec3f
{
    const float* const p = mesh.vertices.single_precision_point_ptr(vertex);
    return GEO::vec3f{p[0], p[1], p[2]};
}

} // anonymous namespace

auto has_self_intersections(const GEO::Mesh& mesh) -> bool
{
    // Triangulate all facets using fan triangulation and collect triangles
    std::vector<Triangle> triangles;

    const GEO::index_t facet_count = mesh.facets.nb();
    for (GEO::index_t f = 0; f < facet_count; ++f) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(f);
        if (corner_count < 3) {
            continue;
        }

        const GEO::index_t first_corner = mesh.facets.corner(f, 0);
        const GEO::index_t v0_idx       = mesh.facet_corners.vertex(first_corner);
        const GEO::vec3f   pos0         = get_vertex_position(mesh, v0_idx);

        for (GEO::index_t c = 1; (c + 1) < corner_count; ++c) {
            const GEO::index_t corner1 = mesh.facets.corner(f, c);
            const GEO::index_t corner2 = mesh.facets.corner(f, c + 1);
            const GEO::index_t v1_idx  = mesh.facet_corners.vertex(corner1);
            const GEO::index_t v2_idx  = mesh.facet_corners.vertex(corner2);

            Triangle tri;
            tri.v[0]              = pos0;
            tri.v[1]              = get_vertex_position(mesh, v1_idx);
            tri.v[2]              = get_vertex_position(mesh, v2_idx);
            tri.vertex_indices[0] = v0_idx;
            tri.vertex_indices[1] = v1_idx;
            tri.vertex_indices[2] = v2_idx;
            tri.facet_index       = f;
            triangles.push_back(tri);
        }
    }

    // O(n^2) pair-wise triangle intersection test
    const std::size_t tri_count = triangles.size();
    for (std::size_t i = 0; i < tri_count; ++i) {
        for (std::size_t j = i + 1; j < tri_count; ++j) {
            // Skip triangles from the same facet
            if (triangles[i].facet_index == triangles[j].facet_index) {
                continue;
            }
            if (triangles_intersect(triangles[i], triangles[j])) {
                return true;
            }
        }
    }

    return false;
}

} // namespace erhe::geometry
