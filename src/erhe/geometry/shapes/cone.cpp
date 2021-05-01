#include "erhe/geometry/shapes/cone.hpp"
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

// Stack numbering example
// with stackDivision = 2
//
// stack                        rel    known
// base0  stack                 stack  as
// -----------------------------------------------------------------------------------------.
//  5      3         /\         1.0    top        (singular = single vertex in this case)   .
//  4      2        /--\                                                                    .
//  3      1       /----\                                                                   .
//  2      0      /======\      0.0    equator                                              .
//  1     -1     /--------\                                                                 .
//  0     -2    /----------\                                                                .
// -1     -3   /============\   -1.0    bottom    (not singular in this case)               .
//
// bottom: relStack = -1.0  <=>  stack = -stackDivision - 1
// top:    relStack =  1.0  <=>  stack =  stackDivision + 1

struct Conical_frustum_builder
{
    Geometry& geometry;

    double min_x         {0.0};
    double max_x         {0.0};
    double bottom_radius {0.0};
    double top_radius    {0.0};
    bool   use_bottom    {false};
    bool   use_top       {false};
    int    slice_count   {0};
    int    stack_division{0};

    int bottom_not_singular{0};
    int top_not_singular   {0};
    int stack_count        {0};

    std::map<std::pair<int, int>, Point_id> points;

    Point_id top_point_id;
    Point_id bottom_point_id;

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

    // relStackIn is in range -1..1
    // relStack is in range 0..1
    auto cone_point(double rel_slice, double rel_stack_minus_one_to_one)
    -> Point_id
    {
        double phi                 = glm::pi<double>() * 2.0 * rel_slice;
        double sin_phi             = std::sin(phi);
        double cos_phi             = std::cos(phi);
        double rel_stack           = 0.5 + 0.5 * rel_stack_minus_one_to_one;
        double one_minus_rel_stack = 1.0 - rel_stack;

        vec3 position(static_cast<float>(one_minus_rel_stack * (min_x                  ) + rel_stack * (max_x)),
                      static_cast<float>(one_minus_rel_stack * (bottom_radius * sin_phi) + rel_stack * (top_radius * sin_phi)),
                      static_cast<float>(one_minus_rel_stack * (bottom_radius * cos_phi) + rel_stack * (top_radius * cos_phi)));

        vec3 bottom(static_cast<float>(min_x),
                    static_cast<float>(bottom_radius * sin_phi),
                    static_cast<float>(bottom_radius * cos_phi));

        vec3 top(static_cast<float>(max_x),
                 static_cast<float>(top_radius * sin_phi),
                 static_cast<float>(top_radius * cos_phi));

        vec3 B = glm::normalize(top - bottom); // generatrix
        vec3 T{0.0f,
               static_cast<float>(std::sin(phi + (glm::pi<double>() * 0.5))),
               static_cast<float>(std::cos(phi + (glm::pi<double>() * 0.5)))};
        vec3 N = glm::cross(B, T);
        N      = glm::normalize(N);

        vec3  t_xyz = glm::normalize(T - N * glm::dot(N, T));
        float t_w   = (glm::dot(glm::cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;
        vec3  b_xyz = glm::normalize(B - N * glm::dot(N, B));
        float b_w   = (glm::dot(glm::cross(B, N), T) < 0.0f) ? -1.0f : 1.0f;

        double s = rel_slice;
        double t = rel_stack;

        Point_id point_id = geometry.make_point();

        point_locations ->put(point_id, position);
        point_normals   ->put(point_id, N);
        point_tangents  ->put(point_id, vec4(t_xyz, t_w));
        point_bitangents->put(point_id, vec4(b_xyz, b_w));
        point_texcoords ->put(point_id, vec2(static_cast<float>(s),          static_cast<float>(t)));
        log_cone.trace("point_id = {:3}, rel_slice = {: 3.1}, rel_stack = {: 3.1}, "
                       "position = {}, texcoord = {}\n",
                       point_id, static_cast<float>(rel_slice), static_cast<float>(rel_stack),
                       position,
                       glm::vec2(s, t));
        return point_id;
    }

    auto make_corner(Polygon_id polygon, int slice, int stack)
    -> Corner_id
    {
        return make_corner(polygon, slice, stack, false);
    }

    auto make_corner(Polygon_id polygon_id, int slice, int stack, bool base)
    -> Corner_id
    {
        auto rel_slice                  = static_cast<double>(slice) / static_cast<double>(slice_count);
        auto rel_stack_minus_one_to_one = static_cast<double>(stack) / (static_cast<double>(stack_division) + 1);
        auto rel_stack                  = 0.5 + 0.5 * rel_stack_minus_one_to_one;

        bool is_slice_seam       = (slice == 0) || (slice == slice_count);
        bool is_bottom           = stack == -stack_division - 1;
        bool is_top              = stack ==  stack_division + 1;
        bool is_uv_discontinuity = is_slice_seam || is_bottom || is_top;

        Point_id point_id{0};

        //log_cone.trace("polygon_id = {:2}, slice = {: 3}, stack = {: 3}, rel_slice = {: 3.1}, rel_stack = {: 3.1}, "
        //               "base = {:>5}, point_id = ", polygon_id, slice, stack, rel_slice, rel_stack, base);
        if (is_top && (top_radius == 0.0))
        {
            point_id = top_point_id;
            //log_cone.trace("{:2} (top)        ", point_id);
        }
        else if (is_bottom && (bottom_radius == 0.0))
        {
            point_id = bottom_point_id;
            //log_cone.trace("{:2} (bottom)     ", point_id);
        }
        else if (slice == slice_count)
        {
            point_id = points[std::make_pair(0, stack)];
            //log_cone.trace("{:2} (slice seam) ", point_id);
        }
        else
        {
            point_id = points[std::make_pair(slice, stack)];
            //log_cone.trace("{:2}              ", point_id);
        }

        Corner_id corner_id = geometry.make_polygon_corner(polygon_id, point_id);
        //log_cone.trace("corner = {:2}", corner_id);

        if (is_uv_discontinuity)
        {
            float s, t;
            if (base)
            {
                double phi                 = glm::pi<double>() * 2.0 * rel_slice;
                double sin_phi             = std::sin(phi);
                double cos_phi             = std::cos(phi);
                double one_minus_rel_stack = 1.0 - rel_stack;

                s = static_cast<float>(one_minus_rel_stack * sin_phi + rel_stack * sin_phi);
                t = static_cast<float>(one_minus_rel_stack * cos_phi + rel_stack * cos_phi);
                //log_cone.trace(" base");
            }
            else
            {
                s = static_cast<float>(rel_slice);
                t = static_cast<float>(rel_stack);
                //log_cone.trace(" slice seam rel_slice = {: 3.1} rel_stack = {: 3.1}", rel_slice, rel_stack);
            }
            corner_texcoords->put(corner_id, vec2(s, t));
            //log_cone.trace(" UV discontinuity, texcoord = {: 3.1}, {: 3.1}", s, t);
        }

        if (is_top || is_bottom)
        {
            if (base && is_bottom && (bottom_radius != 0.0) && use_bottom)
            {
                corner_normals->put(corner_id, vec3(-1.0f, 0.0f, 0.0f));
                corner_tangents->put(corner_id, bottom_not_singular ? vec4(0.0f, 1.0f, 0.0f, 1.0f)
                                                                    : vec4(0.0f, 0.0f, 0.0f, 0.0f));
                corner_bitangents->put(corner_id, bottom_not_singular ? vec4(0.0f, 0.0f, 1.0f, 1.0f)
                                                                      : vec4(0.0f, 0.0f, 0.0f, 0.0f));
                //log_cone.trace(" forced bottom normal and tangent");
            }

            if (base && is_top && (top_radius != 0.0) && use_top)
            {
                corner_normals->put(corner_id, vec3(1.0f, 0.0f, 0.0f));
                corner_tangents->put(corner_id, bottom_not_singular ? vec4(0.0f, 1.0f, 0.0f, 1.0f)
                                                                    : vec4(0.0f, 0.0f, 0.0f, 0.0f));
                corner_bitangents->put(corner_id, bottom_not_singular ? vec4(0.0f, 0.0f, 1.0f, 1.0f)
                                                                      : vec4(0.0f, 0.0f, 0.0f, 0.0f));
                //log_cone.trace(" forced top normal and tangent");
            }
        }
        log_cone.trace("\n");
        return corner_id;
    }

    Conical_frustum_builder(Geometry& geometry,
                            double    min_x,
                            double    max_x,
                            double    bottom_radius,
                            double    top_radius,
                            bool      use_bottom,
                            bool      use_top,
                            int       slice_count,
                            int       stack_division)
        : geometry           {geometry}
        , min_x              {min_x}
        , max_x              {max_x}
        , bottom_radius      {bottom_radius}
        , top_radius         {top_radius}
        , use_bottom         {use_bottom}
        , use_top            {use_top}
        , slice_count        {slice_count}
        , stack_division     {stack_division}
        , bottom_not_singular{(bottom_radius != 0.0) ? 1 : 0}
        , top_not_singular   {(top_radius != 0.0) ? 1 : 0}
        , stack_count        {(2 * stack_division) + 1 + bottom_not_singular + top_not_singular}
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
        // Points
        //log_cone.trace("Points:\n");
        for (int slice = 0; slice < slice_count; ++slice)
        {
            auto rel_slice = static_cast<double>(slice) / static_cast<double>(slice_count);

            for (int stack = -stack_division - bottom_not_singular;
                stack <= stack_division + top_not_singular;
                ++stack)
            {
                auto rel_stack = static_cast<double>(stack) / (static_cast<double>(stack_division) + 1);

                //log_cone.trace("\tslice {:2} stack {: 2}: ", slice, stack);
                points[std::make_pair(slice, stack)] = cone_point(rel_slice, rel_stack);
            }
        }

        // Bottom parts
        //if (false)
        if (bottom_radius == 0.0)
        {
            //log_cone.trace("Bottom - point / triangle fan\n");
            bottom_point_id = geometry.make_point(min_x, 0.0, 0.0); // Apex
            for (int slice = 0; slice < slice_count; ++slice)
            {
                int  stack              = -stack_division; // Second last stack, bottom is -stackDivision - 1
                auto rel_slice_centroid = (static_cast<double>(slice) + 0.5) / static_cast<double>(slice_count);
                auto rel_stack_centroid = -1.0 + (0.5 / (static_cast<double>(stack_division) + 1));

                Polygon_id polygon_id  = geometry.make_polygon();

                make_corner(polygon_id, slice + 1, stack);
                make_corner(polygon_id, slice,     stack);
                Corner_id tip_corner_id = make_corner(polygon_id, slice, -stack_division - 1);
                auto p0 = get_point(slice, stack);
                auto p1 = get_point(slice + 1, stack);

                vec3 n1             = point_normals->get(p0);
                vec3 n2             = point_normals->get(p1);
                vec3 average_normal = normalize(n1 + n2);
                corner_normals->put(tip_corner_id, average_normal);

                vec4 t1              = point_tangents->get(p0);
                vec4 t2              = point_tangents->get(p1);
                vec3 average_tangent = normalize(glm::vec3(t1) + glm::vec3(t2));
                corner_tangents->put(tip_corner_id, glm::vec4(average_tangent, t1.w));

                vec4 b1              = point_bitangents->get(p0);
                vec4 b2              = point_bitangents->get(p1);
                vec3 average_bitangent = normalize(glm::vec3(b1) + glm::vec3(b2));
                corner_bitangents->put(tip_corner_id, glm::vec4(average_bitangent, b1.w));

                vec2 uv1              = point_texcoords->get(p0);
                vec2 uv2              = point_texcoords->get(p1);
                vec2 average_texcoord = (uv1 + uv2) / 2.0f;
                corner_texcoords->put(tip_corner_id, average_texcoord);

                Point_id centroid_id = cone_point(rel_slice_centroid, rel_stack_centroid);
                polygon_centroids->put(polygon_id, point_locations->get(centroid_id));
                polygon_normals->put(polygon_id, point_normals->get(centroid_id));
            }
        }
        else
        {
            if (use_bottom)
            {
                //log_cone.trace("Bottom - flat polygon\n");
                Polygon_id polygon_id = geometry.make_polygon();
                polygon_centroids->put(polygon_id, vec3(static_cast<float>(min_x), 0.0f, 0.0f));
                polygon_normals->put(polygon_id, vec3(-1.0f, 0.0f, 0.0f));

                for (int slice = 0; slice < slice_count; ++slice)
                {
                    int reverse_slice = slice_count - 1 - slice;
                    make_corner(polygon_id, reverse_slice, -stack_division - 1, true);
                }

                geometry.polygons[polygon_id].compute_planar_texture_coordinates(polygon_id,
                                                                                 geometry,
                                                                                 *corner_texcoords,
                                                                                 *polygon_centroids,
                                                                                 *polygon_normals,
                                                                                 *point_locations,
                                                                                 false);
            }
            else
            {
                log_cone.trace("Bottom - none\n");
            }
        }

        // Middle quads, bottom up
        log_cone.trace("Middle quads, bottom up\n");
        //if (false)
        for (int stack = -stack_division - bottom_not_singular;
            stack < stack_division + top_not_singular;
            ++stack)
        {
            double rel_stack_centroid = (static_cast<double>(stack) + 0.5) / (static_cast<double>(stack_division) + 1);

            for (int slice = 0; slice < slice_count; ++slice)
            {
                auto rel_slice_centroid = (static_cast<double>(slice) + 0.5) / static_cast<double>(slice_count);

                Polygon_id polygon_id = geometry.make_polygon();
                make_corner(polygon_id, slice + 1, stack + 1);
                make_corner(polygon_id, slice, stack + 1);
                make_corner(polygon_id, slice, stack);
                make_corner(polygon_id, slice + 1, stack);

                Point_id centroid_id = cone_point(rel_slice_centroid, rel_stack_centroid);
                polygon_centroids->put(polygon_id, point_locations->get(centroid_id));
                polygon_normals->put(polygon_id, point_normals->get(centroid_id));
            }
        }

        // Top parts
        if (top_radius == 0.0)
        {
            log_cone.trace("Top - point / triangle fan\n");
            top_point_id = geometry.make_point(max_x, 0.0, 0.0); //  apex

            for (int slice = 0; slice < slice_count; ++slice)
            {
                int  stack              = stack_division;
                auto rel_slice_centroid = (static_cast<double>(slice) + 0.5) / static_cast<double>(slice_count);
                auto rel_stack_centroid = 1.0 - (0.5 / (static_cast<double>(stack_division) + 1));

                Polygon_id polygon_id    = geometry.make_polygon();
                Corner_id  tip_corner_id = make_corner(polygon_id, slice, stack_division + 1);
                make_corner(polygon_id, slice, stack);
                make_corner(polygon_id, slice + 1, stack);

                auto p0 = get_point(slice, stack);
                auto p1 = get_point(slice + 1, stack);

                vec3 n1             = point_normals->get(p0);
                vec3 n2             = point_normals->get(p1);
                vec3 average_normal = normalize(n1 + n2);
                corner_normals->put(tip_corner_id, average_normal);

                vec4 t1              = point_tangents->get(p0);
                vec4 t2              = point_tangents->get(p1);
                vec3 average_tangent = normalize(glm::vec3(t1) + glm::vec3(t2));
                corner_tangents->put(tip_corner_id, glm::vec4(average_tangent, t1.w));

                vec4 b1                = point_bitangents->get(p0);
                vec4 b2                = point_bitangents->get(p1);
                vec3 average_bitangent = normalize(glm::vec3(b1) + glm::vec3(b2));
                corner_bitangents->put(tip_corner_id, glm::vec4(average_bitangent, b1.w));

                vec2 uv1              = point_texcoords->get(p0);
                vec2 uv2              = point_texcoords->get(p1);
                vec2 average_texcoord = (uv1 + uv2) / 2.0f;
                corner_texcoords->put(tip_corner_id, average_texcoord);

                Point_id centroid_id = cone_point(rel_slice_centroid, rel_stack_centroid);
                polygon_centroids->put(polygon_id, point_locations->get(centroid_id));
                polygon_normals->put(polygon_id, point_normals->get(centroid_id));
            }
        }
        else
        {
            if (use_top)
            {
                log_cone.trace("Top - flat polygon\n");
                Polygon_id polygon_id = geometry.make_polygon();
                polygon_centroids->put(polygon_id, vec3(static_cast<float>(max_x), 0.0f, 0.0f));
                polygon_normals->put(polygon_id, vec3(1.0f, 0.0f, 0.0f));

                for (int slice = 0; slice < slice_count; ++slice)
                {
                    make_corner(polygon_id, slice, stack_division + 1, true);
                }

                geometry.polygons[polygon_id].compute_planar_texture_coordinates(polygon_id,
                                                                                 geometry,
                                                                                 *corner_texcoords,
                                                                                 *polygon_centroids,
                                                                                 *polygon_normals,
                                                                                 *point_locations,
                                                                                 false);
            }
            else
            {
                log_cone.trace("Top - none\n");
            }
        }
        geometry.make_point_corners();
        geometry.build_edges();
        geometry.promise_has_normals();
        geometry.promise_has_tangents();
        geometry.promise_has_bitangents();
        geometry.promise_has_texture_coordinates();
    }
};

auto make_conical_frustum(double min_x,
                          double max_x,
                          double bottom_radius,
                          double top_radius,
                          bool   use_bottom,
                          bool   use_top,
                          int    slice_count,
                          int    stack_division)
-> Geometry
{
    ZoneScoped;

    Geometry geometry{"conical frustum"};
    Conical_frustum_builder builder(geometry,
                                    min_x,
                                    max_x,
                                    bottom_radius,
                                    top_radius,
                                    use_bottom,
                                    use_top,
                                    slice_count,
                                    stack_division);
    builder.build();
    return geometry;
}

auto make_cone(double min_x,
               double max_x,
               double bottom_radius,
               bool   use_bottom,
               int    slice_count,
               int    stack_division)
-> Geometry
{
    ZoneScoped;

    Geometry geometry{"cone"};
    Conical_frustum_builder builder(geometry,
                                    min_x,
                                    max_x,
                                    bottom_radius,
                                    0.0,
                                    use_bottom,
                                    false,
                                    slice_count,
                                    stack_division);
    builder.build();
    return geometry;
}

auto make_cylinder(double min_x,
                   double max_x,
                   double radius,
                   bool   use_bottom,
                   bool   use_top,
                   int    slice_count,
                   int    stack_division)
-> Geometry
{
    ZoneScoped;

    Geometry geometry{"cylinder"};
    Conical_frustum_builder builder(geometry,
                                    min_x,
                                    max_x,
                                    radius,
                                    radius,
                                    use_bottom,
                                    use_top,
                                    slice_count,
                                    stack_division);
    builder.build();
    return geometry;
}

} // namespace erhe::geometry::shapes

