#include "scene/mesh_intersect.hpp"
#include "editor_log.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

namespace {

#define CULLING 0

auto ray_triangle_intersect(
    const GEO::vec3& origin,
    const GEO::vec3& direction,
    const GEO::vec3& v0,
    const GEO::vec3& v1,
    const GEO::vec3& v2,
    double&          out_t,
    double&          out_u,
    double&          out_v
) -> bool
{
    const GEO::vec3 v0v1 = v1 - v0;
    const GEO::vec3 v0v2 = v2 - v0;
    const GEO::vec3 pvec = GEO::cross(direction, v0v2);
    const double det = GEO::dot(v0v1, pvec);
#ifdef CULLING
    constexpr double epsilon = 0.00001f;

    // if the determinant is negative the triangle is backfacing
    // if the determinant is close to 0, the ray misses the triangle
    if (det < epsilon) {
        return false;
    }
#else
    // ray and triangle are parallel if det is close to 0
    if (std::fabs(det) < epsilon) {
        return false;
    }
#endif
    const double inv_det = 1.0 / det;

    const GEO::vec3 tvec = origin - v0;
    double u = GEO::dot(tvec, pvec) * inv_det;
    if ((u < 0.0) || (u > 1.0)) {
        return false;
    }
    const GEO::vec3 qvec = GEO::cross(tvec, v0v1);
    double v = GEO::dot(direction, qvec) * inv_det;
    if ((v < 0.0) || (u + v > 1.0f)) {
        return false;
    }

    double t = GEO::dot(v0v2, qvec) * inv_det;

    out_t = t;
    out_u = u;
    out_v = v;

    return true;
}

} // namespace

auto intersect(
    const erhe::scene::Mesh&                  mesh,
    const GEO::vec3                           origin_in_world,
    const GEO::vec3                           direction_in_world,
    std::shared_ptr<erhe::geometry::Geometry> out_geometry,
    GEO::index_t&                             out_facet,
    double&                                   out_t,
    double&                                   out_u,
    double&                                   out_v
) -> bool
{
    const erhe::scene::Node* node = mesh.get_node();
    ERHE_VERIFY(node != nullptr);
    const glm::mat4 mesh_from_world_  = node->node_from_world();
    const GEO::mat4 mesh_from_world   = to_geo_mat4(mesh_from_world_);
    const GEO::vec3 origin_in_mesh    = GEO::vec3{mesh_from_world * GEO::vec4{origin_in_world, 1.0}};
    const GEO::vec3 direction_in_mesh = GEO::vec3{mesh_from_world * GEO::vec4{direction_in_world, 0.0}};

    out_t = std::numeric_limits<float>::max();

    for (auto& primitive : mesh.get_primitives()) {
        const std::shared_ptr<erhe::primitive::Primitive_shape>& shape = primitive.get_shape_for_raytrace();
        if (!shape) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape->get_geometry_const();
        if (!geometry) {
            continue;
        }
        GEO::Mesh& geo_mesh = geometry->get_mesh();

        //erhe::raytrace::log_geometry.trace("raytrace {}\n", geometry->name);
        for (GEO::index_t facet : geo_mesh.facets) {
            const GEO::index_t first_corner = geo_mesh.facets.corner(facet, 0);
            const GEO::index_t first_vertex = geo_mesh.facet_corners.vertex(first_corner);
            const GEO::vec3    v0           = geo_mesh.vertices.point(first_vertex);

            for (GEO::index_t corner : geo_mesh.facets.corners(facet)) {
                const GEO::index_t next_corner = geo_mesh.facets.next_corner_around_facet(facet, corner);
                const GEO::index_t vertex      = geo_mesh.facet_corners.vertex(corner);
                const GEO::index_t next_vertex = geo_mesh.facet_corners.vertex(next_corner);
                const GEO::vec3    v1          = geo_mesh.vertices.point(vertex);
                const GEO::vec3    v2          = geo_mesh.vertices.point(next_vertex);

                double hit_t;
                double hit_u;
                double hit_v;
                const bool hit = ray_triangle_intersect(origin_in_mesh, direction_in_mesh, v0, v1, v2, hit_t, hit_u, hit_v);
                if (hit) {
                    log_raytrace->trace(
                        "hit polygon {} {}-{}-{} with t = {}",
                        facet,
                        first_vertex,
                        vertex,
                        next_vertex,
                        hit_t
                    );
                }
                if (hit && (hit_t < out_t)) {
                    out_geometry = geometry;
                    out_facet    = facet;
                    out_t        = hit_t;
                    out_u        = hit_u;
                    out_v        = hit_v;
                }
            }
        }
    }

    if (out_t != std::numeric_limits<float>::max()) {
        log_raytrace->trace("final hit polygon {} with t = {}", out_facet, out_t);
    }
    return out_t != std::numeric_limits<float>::max();
}

} // namespace editor
