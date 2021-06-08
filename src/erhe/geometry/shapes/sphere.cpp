#include "erhe/geometry/shapes/sphere.hpp"
#include "erhe/geometry/log.hpp"
#include "Tracy.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <gsl/assert>

#include <map>
#include <vector>

namespace erhe::geometry::shapes
{

using glm::vec2;
using glm::vec3;
using glm::vec4;

namespace
{

// Using geometric centroids, centroids are floating above the polygon.
// The distance from the polygon is more when using small subdivision.
constexpr const bool use_geometric_centroids = false;

// Using flat centroids ensures centroids are on polygon.
constexpr const bool use_flat_centroids      = true;

struct Sphere_builder
{
    Geometry& geometry;
    double    radius;
    int       slice_count;
    int       stack_division;
    int       stack_count;
    int       stack_base0_bottom;
    int       stack_base0_top;

    std::map<std::pair<int, int>, Point_id> points;
    Point_id                                top_point_id{0};
    Point_id                                bottom_point_id{0};

    Property_map<Point_id  , vec3>* point_locations  {nullptr};
    Property_map<Point_id  , vec3>* point_normals    {nullptr};
    Property_map<Point_id  , vec4>* point_tangents   {nullptr};
    Property_map<Point_id  , vec4>* point_bitangents {nullptr};
    Property_map<Point_id  , vec2>* point_texcoords  {nullptr};
    Property_map<Corner_id , vec3>* corner_normals   {nullptr};
    Property_map<Corner_id , vec4>* corner_tangents  {nullptr};
    Property_map<Corner_id , vec4>* corner_bitangents{nullptr};
    Property_map<Corner_id , vec2>* corner_texcoords {nullptr};
    Property_map<Polygon_id, vec3>* polygon_centroids{nullptr};
    Property_map<Polygon_id, vec3>* polygon_normals  {nullptr};

    auto get_point(int slice, int stack)
    -> Point_id
    {
        return points[std::make_pair(slice, stack)];
    }

    void set_point(int slice, int stack, Point_id point_id)
    {
        points[std::make_pair(slice, stack)] = point_id;
    }

    auto sphere_point(double rel_slice, double rel_stack)
    -> Point_id
    {
        const double phi     = (glm::pi<double>() * 2.0 * rel_slice);
        const double sin_phi = std::sin(phi);
        const double cos_phi = std::cos(phi);

        const double theta     = (glm::pi<double>() * 0.5 * rel_stack);
        const double sin_theta = std::sin(theta);
        const double cos_theta = std::cos(theta);

        const double stack_radius = cos_theta;

        const auto xVN = static_cast<float>(stack_radius * cos_phi);
        const auto yVN = static_cast<float>(sin_theta);
        const auto zVN = static_cast<float>(stack_radius * sin_phi);

        const auto xP = static_cast<float>(radius * xVN);
        const auto yP = static_cast<float>(radius * yVN);
        const auto zP = static_cast<float>(radius * zVN);

        const auto s = 1.0f - static_cast<float>(rel_slice);
        const auto t = 1.0f - static_cast<float>(0.5 * (1.0 + rel_stack));

        const vec3     N(xVN, yVN, zVN);
        const vec3     axis(0.0f, 1.0f, 0.0f);
        const vec3     B        = glm::normalize(glm::cross(axis, N));
        const vec3     T        = glm::normalize(glm::cross(N, B));
        const Point_id point_id = geometry.make_point();

        const vec3  t_xyz = glm::normalize(T - N * glm::dot(N, T));
        const float t_w   = (glm::dot(glm::cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;
        const vec3  b_xyz = glm::normalize(B - N * glm::dot(N, B));
        const float b_w   = (glm::dot(glm::cross(B, N), T) < 0.0f) ? -1.0f : 1.0f;

        //bool is_uv_discontinuity = (rel_stack == -1.0) ||
        //                           (rel_stack ==  1.0) ||
        //                        // (relSlice == 0.0) ||
        //                           (rel_slice ==  1.0);

        const vec3 P = vec3(xP, yP, zP);

        point_locations ->put(point_id, P);
        point_normals   ->put(point_id, N);
        point_tangents  ->put(point_id, vec4(t_xyz, t_w));
        point_bitangents->put(point_id, vec4(b_xyz, b_w));
        //if (!is_uv_discontinuity)
        {
            point_texcoords->put(point_id, vec2(s, t));
        }

        //log_sphere.trace("point_id = {:3}, rel_slice = {: #07f}, rel_stack = {: #07f}, "
        //                 "position = {} normal = }\n",
        //                 point_id, static_cast<float>(rel_slice), static_cast<float>(rel_stack),
        //                 P,
        //                 N);

        return point_id;
    }

    Corner_id make_corner(Polygon_id polygon_id, int slice, int stack_base0)
    {
        Expects(slice >= 0);

        //int stack_base0 = stack + stackDivision;
        const int    stack               = stack_base0 - stack_division;
        const double rel_slice           = static_cast<double>(slice) / static_cast<double>(slice_count);
        const double rel_stack           = static_cast<double>(stack) / (static_cast<double>(stack_division) + 1);
        const bool   is_slice_seam       = /*(slice == 0) ||*/ (slice == slice_count);
        const bool   is_bottom           = (stack_base0 == stack_base0_bottom);
        const bool   is_top              = (stack_base0 == stack_base0_top);
        const bool   is_uv_discontinuity = is_slice_seam || is_bottom || is_top;

        //log_sphere.trace("\tpolygon {:3} slice = {: 2} stack_base0 = {: 2} rel_slice = {: #08f} rel_stack = {: #08f} using ",
        //                 polygon_id, slice, stack_base0, rel_slice, rel_stack);

        Point_id point_id;
        if (is_top)
        {
            point_id = top_point_id;
            //log_sphere.trace("point id {:2} (top)", point_id);
        }
        else if (is_bottom)
        {
            point_id = bottom_point_id;
            //log_sphere.trace("point id {:2} (bottom)", point_id);
        }                              
        else if (slice == slice_count)
        {
            point_id = points[std::make_pair(0, stack)];
            //log_sphere.trace("point id {:2} (slice seam)", point_id);
        }
        else
        {
            point_id = points[std::make_pair(slice, stack)];
            //log_sphere.trace("point id {:2}", point_id);
        }

        const Corner_id corner_id = geometry.make_polygon_corner(polygon_id, point_id);
        //log_sphere.trace(" corner id = {:2}\n", corner_id);

        if (is_uv_discontinuity)
        {
            auto s = 1.0f - static_cast<float>(rel_slice);
            auto t = 1.0f - static_cast<float>(0.5 * (1.0 + rel_stack));

            corner_texcoords->put(corner_id, vec2(s, t));
        }

        return corner_id;
    }

    Sphere_builder(Geometry& geometry, double radius, int slice_count, int stack_division)
        : geometry          {geometry}
        , radius            {radius}
        , slice_count       {slice_count}
        , stack_division    {stack_division}
        , stack_count       {(2 * stack_division) + 1}
        , stack_base0_bottom{-1}
        , stack_base0_top   {static_cast<int>(2 * stack_division + 1)}
    {
        point_locations   = geometry.point_attributes  ().create<vec3>(c_point_locations  );
        point_normals     = geometry.point_attributes  ().create<vec3>(c_point_normals    );
        point_tangents    = geometry.point_attributes  ().create<vec4>(c_point_tangents   );
        point_bitangents  = geometry.point_attributes  ().create<vec4>(c_point_bitangents );
        point_texcoords   = geometry.point_attributes  ().create<vec2>(c_point_texcoords  );
        polygon_centroids = geometry.polygon_attributes().create<vec3>(c_polygon_centroids);
        polygon_normals   = geometry.polygon_attributes().create<vec3>(c_polygon_normals  );
        corner_normals    = geometry.corner_attributes ().create<vec3>(c_corner_normals   );
        corner_tangents   = geometry.corner_attributes ().create<vec4>(c_corner_tangents  );
        corner_bitangents = geometry.corner_attributes ().create<vec4>(c_corner_bitangents);
        corner_texcoords  = geometry.corner_attributes ().create<vec2>(c_corner_texcoords );
    }

    void build()
    {
        bottom_point_id = sphere_point(0.5f, -1.0f);
        top_point_id    = sphere_point(0.5f,  1.0f);
        point_normals->put(bottom_point_id,  vec3(0.0f,-1.0f, 0.0f));
        point_normals->put(top_point_id,     vec3(0.0f, 1.0f, 0.0f));
        point_tangents->put(bottom_point_id, vec4(0.0f, 0.0f, 0.0f, 0.0f));
        point_tangents->put(top_point_id,    vec4(0.0f, 0.0f, 0.0f, 0.0f));

        for (int slice = 0; slice < slice_count; ++slice)
        {
            const double rel_slice = static_cast<double>(slice) / static_cast<double>(slice_count);
            for (int stack = -stack_division; stack <= stack_division; ++stack)
            {
                const double   rel_stack = static_cast<double>(stack) / static_cast<double>(stack_division);
                const Point_id point_id  = sphere_point(rel_slice, rel_stack);
                set_point(slice, stack, point_id);
                //log_sphere.trace("point id {:2} is rel slice {: #08f} rel stack {: #08f}\n", point_id, rel_slice, rel_stack);
            }
        }

        //log_sphere.trace("bottom point id = {}\n", bottom_point_id);
        //log_sphere.trace("top point id    = {}\n", top_point_id);

        // Bottom fan
        log_sphere.trace("bottom fan\n");
        {
            for (int slice = 0; slice < slice_count; ++slice)
            {
                const int        next_slice  = slice + 1;
                const int        stack       = -stack_division;
                const int        stack_base0 = stack + stack_division;
                const double     rel_slice   = (static_cast<double>(slice) + 0.5) / static_cast<double>(slice_count);
                const double     rel_stack   = 1.0 - (0.5 / (static_cast<double>(stack) + 1));
                const Point_id   centroid_id = sphere_point(rel_slice, rel_stack);
                const Polygon_id polygon_id  = geometry.make_polygon();
                make_corner(polygon_id, next_slice, stack_base0);
                make_corner(polygon_id, slice,      stack_base0);
                const Corner_id tip_corner_id = make_corner(polygon_id, slice, stack_base0_bottom);
                const auto      p0            = get_point(next_slice, stack);
                const auto      p1            = get_point(slice,      stack);

                const vec3 n1             = point_normals->get(p0);
                const vec3 n2             = point_normals->get(p1);
                const vec3 average_normal = normalize(n1 + n2);
                corner_normals->put(tip_corner_id, average_normal);

                const vec4 t1              = point_tangents->get(p0);
                const vec4 t2              = point_tangents->get(p1);
                const vec3 average_tangent = normalize(glm::vec3(t1) + glm::vec3(t2));
                corner_tangents->put(tip_corner_id, glm::vec4(average_tangent, t1.w));

                const vec4 b1                = point_bitangents->get(p0);
                const vec4 b2                = point_bitangents->get(p1);
                const vec3 average_bitangent = normalize(glm::vec3(b1) + glm::vec3(b2));
                corner_bitangents->put(tip_corner_id, glm::vec4(average_bitangent, b1.w));

                const vec2 uv1              = point_texcoords->get(p0);
                const vec2 uv2              = point_texcoords->get(p1);
                const vec2 average_texcoord = (uv1 + uv2) / 2.0f;
                corner_texcoords->put(tip_corner_id, average_texcoord);

                if constexpr (use_geometric_centroids)
                {
                    polygon_centroids->put(polygon_id, point_locations->get(centroid_id));
                }
                polygon_normals->put(polygon_id, point_normals->get(centroid_id));
            }
        }

        // Middle quads, bottom up
        for (int stack = -stack_division; stack < stack_division; ++stack)
        {
            const int    stack_base0      = stack + stack_division;
            const int    next_stack_base0 = stack_base0 + 1;
            const double rel_stack        = (static_cast<double>(stack) + 0.5) / (static_cast<double>(stack_division) + 1);
            for (int slice = 0; slice < slice_count; ++slice)
            {
                const int        next_slice  = (slice + 1);
                const double     rel_slice   = (static_cast<double>(slice) + 0.5) / static_cast<double>(slice_count);
                const Point_id   centroid_id = sphere_point(rel_slice, rel_stack);
                const Polygon_id polygon_id  = geometry.make_polygon();
                make_corner(polygon_id, next_slice, next_stack_base0);
                make_corner(polygon_id, slice, next_stack_base0);
                make_corner(polygon_id, slice, stack_base0);
                make_corner(polygon_id, next_slice, stack_base0);

                if constexpr (use_geometric_centroids)
                {
                    polygon_centroids->put(polygon_id, point_locations->get(centroid_id));
                }
                polygon_normals->put(polygon_id, point_normals->get(centroid_id));
            }
        }

        // Top fan
        log_sphere.trace("top fan\n");
        for (int slice = 0; slice < slice_count; ++slice)
        {
            const int        next_slice    = (slice + 1);
            const int        stack         = stack_division;
            const int        stack_base0   = stack + stack_division;
            const double     rel_slice     = (static_cast<double>(slice) + 0.5) / static_cast<double>(slice_count);
            const double     rel_stack     = 1.0 - (0.5 / (static_cast<double>(stack) + 1));
            const Point_id   centroid_id   = sphere_point(rel_slice, rel_stack);
            const Polygon_id polygon_id    = geometry.make_polygon();
            const Corner_id  tip_corner_id = make_corner(polygon_id, slice, stack_base0_top);
            make_corner(polygon_id, slice,      stack_base0);
            make_corner(polygon_id, next_slice, stack_base0);

            const Point_id p0 = get_point(slice,      stack);
            const Point_id p1 = get_point(next_slice, stack);

            const vec3 n1 = point_normals->get(p0);
            const vec3 n2 = point_normals->get(p1);
            const vec3 n  = normalize(n1 + n2);
            corner_normals->put(tip_corner_id, n);

            const vec4 t1              = point_tangents->get(p0);
            const vec4 t2              = point_tangents->get(p1);
            const vec3 average_tangent = normalize(glm::vec3(t1) + glm::vec3(t2));
            corner_tangents->put(tip_corner_id, glm::vec4(average_tangent, t1.w));

            const vec4 b1                = point_bitangents->get(p0);
            const vec4 b2                = point_bitangents->get(p1);
            const vec3 average_bitangent = normalize(glm::vec3(b1) + glm::vec3(b2));
            corner_bitangents->put(tip_corner_id, glm::vec4(average_bitangent, b1.w));

            const vec2 uv1              = point_texcoords->get(p0);
            const vec2 uv2              = point_texcoords->get(p1);
            const vec2 average_texcoord = (uv1 + uv2) / 2.0f;
            corner_texcoords->put(tip_corner_id, average_texcoord);

            if constexpr (use_geometric_centroids)
            {
                polygon_centroids->put(polygon_id, point_locations->get(centroid_id));
            }
            polygon_normals->put(polygon_id, point_normals->get(centroid_id));
        }

        geometry.make_point_corners();
        geometry.build_edges();
        if constexpr (use_flat_centroids)
        {
            geometry.compute_polygon_centroids();
        }
        geometry.promise_has_polygon_normals();
        geometry.promise_has_normals();
        geometry.promise_has_tangents();
        geometry.promise_has_bitangents();
        geometry.promise_has_texture_coordinates();
    }
};

} // namespace

auto make_sphere(double radius, unsigned int slice_count, unsigned int stack_division)
-> Geometry
{
    ZoneScoped;

    return Geometry("sphere", [=](auto& geometry) {
        Sphere_builder builder(geometry, radius, static_cast<int>(slice_count), static_cast<int>(stack_division));
        builder.build();
    });
}

} // namespace erhe::geometry::shapes
