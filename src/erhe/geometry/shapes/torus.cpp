#include "erhe/geometry/shapes/torus.hpp"
#include "Tracy.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <map>
#include <vector>

namespace erhe::geometry::shapes
{

using glm::vec2;
using glm::vec3;
using glm::vec4;

// Using geometric centroids, centroids are floating above the polygon.
// The distance from the polygon is more when using small subdivision.
constexpr bool use_geometric_centroids = false;

// Using flat centroids ensures centroids are on polygon.
constexpr bool use_flat_centroids      = true;


class Torus_builder
{
public:
    Geometry& geometry;

    double major_radius;
    double minor_radius;
    int    major_axis_steps;
    int    minor_axis_steps;

    std::vector<Point_id> points;

    Property_map<Point_id  , vec3>* point_locations  {nullptr};
    Property_map<Point_id  , vec3>* point_normals    {nullptr};
    Property_map<Point_id  , vec4>* point_tangents   {nullptr};
    Property_map<Point_id  , vec4>* point_bitangents {nullptr};
    Property_map<Point_id  , vec2>* point_texcoords  {nullptr};
    Property_map<Corner_id , vec2>* corner_texcoords {nullptr};
    Property_map<Polygon_id, vec3>* polygon_centroids{nullptr};
    Property_map<Polygon_id, vec3>* polygon_normals  {nullptr};

    auto torus_point(const double rel_major, const double rel_minor)
    -> Point_id
    {
        const double R         = major_radius;
        const double r         = minor_radius;
        const double theta     = (glm::pi<double>() * 2.0 * rel_major);
        const double phi       = (glm::pi<double>() * 2.0 * rel_minor);
        const double sin_theta = std::sin(theta);
        const double cos_theta = std::cos(theta);
        const double sin_phi   = std::sin(phi);
        const double cos_phi   = std::cos(phi);

        const double vx = (R + r * cos_phi) * cos_theta;
        const double vz = (R + r * cos_phi) * sin_theta;
        const double vy =      r * sin_phi;
        const vec3   V{vx, vy, vz};

        const double tx = -sin_theta;
        const double tz =  cos_theta;
        const double ty = 0.0f;
        const vec3   T{tx, ty, tz};

        const double bx = -sin_phi * cos_theta;
        const double bz = -sin_phi * sin_theta;
        const double by =  cos_phi;
        const vec3   B{bx, by, bz};
        const vec3   N = glm::normalize(glm::cross(B, T));

        const Point_id point_id = geometry.make_point();

        const vec3  t_xyz = glm::normalize(T - N * glm::dot(N, T));
        const float t_w   = (glm::dot(glm::cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;
        const vec3  b_xyz = glm::normalize(B - N * glm::dot(N, B));
        const float b_w   = (glm::dot(glm::cross(B, N), T) < 0.0f) ? -1.0f : 1.0f;

        const bool is_uv_discontinuity = (rel_major == 1.0) || (rel_minor == 1.0);

        point_locations ->put(point_id, V);
        point_normals   ->put(point_id, N);
        point_tangents  ->put(point_id, vec4{t_xyz, t_w});
        point_bitangents->put(point_id, vec4{b_xyz, b_w});
        if (!is_uv_discontinuity)
        {
            point_texcoords->put(point_id, vec2{rel_major, rel_minor});
        }

        // log_torus.trace("point_id = {:3}, rel_major = {: 3.1}, rel_minor = {: 3.1}, position = {}\n",
        //                 point_id, static_cast<float>(rel_major), static_cast<float>(rel_minor), V);

        return point_id;
    }

    void make_corner(const Polygon_id polygon_id, int major, int minor)
    {
        const auto rel_major           = static_cast<double>(major) / static_cast<double>(major_axis_steps);
        const auto rel_minor           = static_cast<double>(minor) / static_cast<double>(minor_axis_steps);
        const bool is_major_seam       = (major == major_axis_steps);
        const bool is_minor_seam       = (minor == minor_axis_steps);
        const bool is_uv_discontinuity = is_major_seam || is_minor_seam;

        if (is_major_seam)
        {
            major = 0;
        }

        if (is_minor_seam)
        {
            minor = 0;
        }

        const Point_id point_id = points[
            (
                static_cast<size_t>(major) * static_cast<size_t>(minor_axis_steps)
            ) +
            static_cast<size_t>(minor)
        ];

        const Corner_id corner_id = geometry.make_polygon_corner(polygon_id, point_id);

        if (is_uv_discontinuity)
        {
            const auto t = static_cast<float>(rel_minor);
            const auto s = static_cast<float>(rel_major);

            corner_texcoords->put(corner_id, vec2{s, t});
        }

        // log_torus.trace("polygon_id = {:2}, major = {: 3}, minor = {: 3}, rel_major = {: 3.1}, rel_minor = {: 3.1}, "
        //                 "is_major_seam = {:>5}, is_minor_seam = {:>5} point_id = {:3} corner_id = {:3}\n",
        //                 polygon_id, major, minor, rel_major, rel_minor,
        //                 is_major_seam, is_minor_seam, point_id, corner_id);
    }

    Torus_builder(
        Geometry&    geometry,
        const double major_radius,
        const double minor_radius,
        const int    major_axis_steps,
        const int    minor_axis_steps
    )
        : geometry        {geometry}
        , major_radius    {major_radius}
        , minor_radius    {minor_radius}
        , major_axis_steps{major_axis_steps}
        , minor_axis_steps{minor_axis_steps}
    {
        point_locations   = geometry.point_attributes()  .create<vec3>(c_point_locations  );
        point_normals     = geometry.point_attributes()  .create<vec3>(c_point_normals    );
        point_tangents    = geometry.point_attributes()  .create<vec4>(c_point_tangents   );
        point_bitangents  = geometry.point_attributes()  .create<vec4>(c_point_bitangents );
        point_texcoords   = geometry.point_attributes()  .create<vec2>(c_point_texcoords  );
        polygon_centroids = geometry.polygon_attributes().create<vec3>(c_polygon_centroids);
        polygon_normals   = geometry.polygon_attributes().create<vec3>(c_polygon_normals  );
        corner_texcoords  = geometry.corner_attributes() .create<vec2>(c_corner_texcoords );
    }

    void build()
    {
        for (int major = 0; major < major_axis_steps; ++major)
        {
            const auto rel_major = static_cast<double>(major) / static_cast<double>(major_axis_steps);
            for (int minor = 0; minor < minor_axis_steps; ++minor)
            {
                auto           rel_minor = static_cast<double>(minor) / static_cast<double>(minor_axis_steps);
                const Point_id point_id  = torus_point(rel_major, rel_minor);
                points.push_back(point_id);
            }
        }

        for (int major = 0; major < major_axis_steps; ++major)
        {
            const int  next_major     = (major + 1);
            const auto rel_major      = static_cast<double>(major) / static_cast<double>(major_axis_steps);
            const auto rel_next_major = static_cast<double>(next_major) / static_cast<double>(major_axis_steps);
            const auto avg_major      = (rel_major + rel_next_major) / 2.0;

            for (int minor = 0; minor < minor_axis_steps; ++minor)
            {
                int  next_minor     = (minor + 1);
                auto rel_minor      = static_cast<double>(minor) / static_cast<double>(minor_axis_steps);
                auto rel_next_minor = static_cast<double>(next_minor) / static_cast<double>(minor_axis_steps);
                auto avg_minor      = (rel_minor + rel_next_minor) / 2.0;
                Point_id   centroid_id = torus_point(avg_major, avg_minor);
                Polygon_id polygon_id  = geometry.make_polygon();
                make_corner(polygon_id, next_major, next_minor);
                make_corner(polygon_id, major,      next_minor);
                make_corner(polygon_id, major,      minor);
                make_corner(polygon_id, next_major, minor);

                if constexpr (use_geometric_centroids)
                {
                    polygon_centroids->put(polygon_id, point_locations->get(centroid_id));
                }
                polygon_normals->put(polygon_id, point_normals->get(centroid_id));
            }
        }

        geometry.make_point_corners();
        geometry.build_edges();
        geometry.promise_has_polygon_normals();
        if constexpr (use_flat_centroids)
        {
            geometry.compute_polygon_centroids();
        }
        geometry.promise_has_normals();
        geometry.promise_has_tangents();
        geometry.promise_has_bitangents();
        geometry.promise_has_texture_coordinates();
    }
};

auto make_torus(
    const double major_radius,
    const double minor_radius,
    const int    major_axis_steps,
    const int    minor_axis_steps
)
-> Geometry
{
    ZoneScoped;

    return Geometry{
        "torus",
        [=](auto& geometry)
        {
        Torus_builder builder{geometry, major_radius, minor_radius, major_axis_steps, minor_axis_steps};
            builder.build();
        }
    };
}

auto torus_volume(const float major_radius, const float minor_radius) -> float
{
    return glm::pi<float>() * minor_radius * minor_radius * 2.0f * glm::pi<float>() * major_radius;
}

} // namespace erhe::geometry::shapes
