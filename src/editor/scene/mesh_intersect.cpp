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
    const GEO::vec3f& origin,
    const GEO::vec3f& direction,
    const GEO::vec3f& v0,
    const GEO::vec3f& v1,
    const GEO::vec3f& v2,
    float&          out_t,
    float&          out_u,
    float&          out_v
) -> bool
{
    const GEO::vec3f v0v1 = v1 - v0;
    const GEO::vec3f v0v2 = v2 - v0;
    const GEO::vec3f pvec = GEO::cross(direction, v0v2);
    const float det = GEO::dot(v0v1, pvec);
#ifdef CULLING
    constexpr float epsilon = 0.00001f;

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
    const float inv_det = 1.0f / det;

    const GEO::vec3f tvec = origin - v0;
    float u = GEO::dot(tvec, pvec) * inv_det;
    if ((u < 0.0f) || (u > 1.0f)) {
        return false;
    }
    const GEO::vec3f qvec = GEO::cross(tvec, v0v1);
    float v = GEO::dot(direction, qvec) * inv_det;
    if ((v < 0.0) || (u + v > 1.0f)) {
        return false;
    }

    float t = GEO::dot(v0v2, qvec) * inv_det;

    out_t = t;
    out_u = u;
    out_v = v;

    return true;
}

} // namespace

#if 0
auto intersect(
    const erhe::scene::Mesh&                  mesh,
    const GEO::vec3f                          origin_in_world,
    const GEO::vec3f                          direction_in_world,
    std::shared_ptr<erhe::geometry::Geometry> out_geometry,
    GEO::index_t&                             out_facet,
    float&                                    out_t,
    float&                                    out_u,
    float&                                    out_v
) -> bool
{
    const erhe::scene::Node* node = mesh.get_node();
    ERHE_VERIFY(node != nullptr);
    const glm::mat4  mesh_from_world_  = node->node_from_world();
    const GEO::mat4f mesh_from_world   = to_geo_mat4(mesh_from_world_);
    const GEO::vec3f origin_in_mesh    = GEO::vec3{mesh_from_world * GEO::vec4{origin_in_world, 1.0}};
    const GEO::vec3f direction_in_mesh = GEO::vec3{mesh_from_world * GEO::vec4{direction_in_world, 0.0}};

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
            const GEO::vec3f   v0           = get_pointf(geo_mesh.vertices, first_vertex);

            for (GEO::index_t corner : geo_mesh.facets.corners(facet)) {
                const GEO::index_t next_corner = geo_mesh.facets.next_corner_around_facet(facet, corner);
                const GEO::index_t vertex      = geo_mesh.facet_corners.vertex(corner);
                const GEO::index_t next_vertex = geo_mesh.facet_corners.vertex(next_corner);
                const GEO::vec3f   v1          = get_pointf(geo_mesh.vertices, vertex);
                const GEO::vec3f   v2          = get_pointf(geo_mesh.vertices, next_vertex);

                float hit_t;
                float hit_u;
                float hit_v;
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
#endif

}
