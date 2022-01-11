#include "erhe/raytrace/mesh_intersect.hpp"
#include "erhe/raytrace/log.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/scene/mesh.hpp"

namespace erhe::raytrace
{

using erhe::geometry::c_point_locations;
using erhe::geometry::Corner;
using erhe::geometry::Corner_id;
using erhe::geometry::Point_id;
using erhe::geometry::Polygon_id;
using erhe::geometry::Polygon_corner_id;
using glm::vec3;
using glm::vec4;

namespace
{

#define CULLING 0

auto ray_triangle_intersect(
    const vec3& origin,
    const vec3& direction,
    const vec3& v0,
    const vec3& v1,
    const vec3& v2,
    float& t,
    float& u,
    float& v
) -> bool
{
    const vec3 v0v1 = v1 - v0;
    const vec3 v0v2 = v2 - v0;
    const vec3 pvec = glm::cross(direction, v0v2);
    const float det = glm::dot(v0v1, pvec);
#ifdef CULLING
    constexpr float epsilon = 0.00001f;

    // if the determinant is negative the triangle is backfacing
    // if the determinant is close to 0, the ray misses the triangle
    if (det < epsilon)
    {
        return false;
    }
#else
    // ray and triangle are parallel if det is close to 0
    if (std::fabs(det) < epsilon)
    {
        return false;
    }
#endif
    const float inv_det = 1.0f / det;

    const vec3 tvec = origin - v0;
    u = glm::dot(tvec, pvec) * inv_det;
    if ((u < 0.0f) || (u > 1.0f))
    {
        return false;
    }
    const vec3 qvec = glm::cross(tvec, v0v1);
    v = glm::dot(direction, qvec) * inv_det;
    if ((v < 0.0f) || (u + v > 1.0f))
    {
        return false;
    }

    t = glm::dot(v0v2, qvec) * inv_det;

    return true;
}

}

auto intersect(
    const erhe::scene::Mesh&    mesh,
    const vec3                  origin_in_world,
    const vec3                  direction_in_world,
    erhe::geometry::Geometry*&  out_geometry,
    erhe::geometry::Polygon_id& out_polygon_id,
    float&                      out_t,
    float&                      out_u,
    float&                      out_v
) -> bool
{
    const auto mesh_from_world   = mesh.node_from_world();
    const vec3 origin_in_mesh    = vec3{mesh_from_world * vec4{origin_in_world, 1.0f}};
    const vec3 direction_in_mesh = vec3{mesh_from_world * vec4{direction_in_world, 0.0f}};

    out_t = std::numeric_limits<float>::max();

    for (auto& primitive : mesh.data.primitives)
    {
        //const auto& primitive_geometry = primitive.gl_primitive_geometry;
        auto* geometry = primitive.source_geometry.get();
        if (geometry == nullptr)
        {
            continue;
        }

        const auto* const point_locations = geometry->point_attributes().find<vec3>(c_point_locations);
        if (point_locations == nullptr)
        {
            return false;
        }

        //erhe::raytrace::log_geometry.trace("raytrace {}\n", geometry->name);
        geometry->for_each_polygon_const([&](auto& i)
        {
            const Corner_id first_corner_id = geometry->polygon_corners[i.polygon.first_polygon_corner_id];
            const Corner&   first_corner    = geometry->corners[first_corner_id];
            const Point_id  first_point_id  = first_corner.point_id;
            const vec3 v0 = point_locations->get(first_point_id);

            for (
                Polygon_corner_id j = 0;
                j < i.polygon.corner_count;
                ++j
            )
            {
                const size_t    corner_index      = static_cast<size_t>(i.polygon.first_polygon_corner_id) + static_cast<size_t>(j);
                const size_t    next_corner_index = static_cast<size_t>(i.polygon.first_polygon_corner_id) + (static_cast<size_t>(j) + 1) % static_cast<size_t>(i.polygon.corner_count);
                const Corner_id corner_id         = geometry->polygon_corners[corner_index];
                const Corner_id next_corner_id    = geometry->polygon_corners[next_corner_index];
                const Corner&   corner            = geometry->corners[corner_id];
                const Corner&   next_corner       = geometry->corners[next_corner_id];
                const Point_id  point_id          = corner.point_id;
                const Point_id  next_point_id     = next_corner.point_id;
                const vec3      v1                = point_locations->get(point_id);
                const vec3      v2                = point_locations->get(next_point_id);

                float hit_t;
                float hit_u;
                float hit_v;
                const bool hit = ray_triangle_intersect(origin_in_mesh, direction_in_mesh, v0, v1, v2, hit_t, hit_u, hit_v);
                if (hit)
                {
                    log_geometry.trace(
                        "hit polygon {} {}-{}-{} with t = {}\n",
                        i.polygon_id,
                        first_point_id,
                        point_id,
                        next_point_id,
                        hit_t
                    );
                }
                if (hit && (hit_t < out_t))
                {
                    out_geometry   = geometry;
                    out_polygon_id = i.polygon_id;
                    out_t          = hit_t;
                    out_u          = hit_u;
                    out_v          = hit_v;
                }
            }
        });
    }

    if (out_t != std::numeric_limits<float>::max())
    {
        log_geometry.trace("final hit polygon {} with t = {}\n", out_polygon_id, out_t);
    }
    return out_t != std::numeric_limits<float>::max();
}

} // namespace
