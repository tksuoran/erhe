#include "erhe/geometry/shapes/box.hpp"
#include "Tracy.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <map>
#include <memory>

namespace erhe::geometry::shapes
{

using glm::vec2;
using glm::vec3;
using glm::ivec3;
using glm::vec4;

namespace
{

auto sign(float x)
-> float
{
    return x < 0.0f ? -1.0f : 1.0f;
}

auto signed_pow(float x, float p)
-> float
{
    return sign(x) * std::pow(std::abs(x), p);
}

} // namespace

auto make_box(double x_size, double y_size, double z_size)
-> Geometry
{
    const double x = x_size / 2.0;
    const double y = y_size / 2.0;
    const double z = z_size / 2.0;

    return Geometry("box", [=](auto& geometry) {
        geometry.make_point(-x,  y,  z, 0, 1); // 0
        geometry.make_point( x,  y,  z, 1, 1); // 1
        geometry.make_point( x, -y,  z, 1, 1); // 2
        geometry.make_point(-x, -y,  z, 0, 1); // 3
        geometry.make_point(-x,  y, -z, 0, 0); // 4
        geometry.make_point( x,  y, -z, 1, 0); // 5
        geometry.make_point( x, -y, -z, 1, 0); // 6
        geometry.make_point(-x, -y, -z, 0, 0); // 7

        geometry.make_polygon( {0, 1, 2, 3} );
        geometry.make_polygon( {0, 3, 7, 4} );
        geometry.make_polygon( {0, 4, 5, 1} ); // top
        geometry.make_polygon( {5, 4, 7, 6} );
        geometry.make_polygon( {2, 1, 5, 6} );
        geometry.make_polygon( {7, 3, 2, 6} ); // bottom

        geometry.make_point_corners();
        geometry.build_edges();
        geometry.compute_polygon_normals();
        geometry.compute_polygon_centroids();
    });
}

auto make_box(float min_x, float max_x, float min_y, float max_y, float min_z, float max_z)
-> Geometry
{
    return Geometry("box", [=](auto& geometry) {
        geometry.make_point(min_x, max_y, max_z, 0, 1); // 0
        geometry.make_point(max_x, max_y, max_z, 1, 1); // 1
        geometry.make_point(max_x, min_y, max_z, 1, 1); // 2
        geometry.make_point(min_x, min_y, max_z, 0, 1); // 3
        geometry.make_point(min_x, max_y, min_z, 0, 0); // 4
        geometry.make_point(max_x, max_y, min_z, 1, 0); // 5
        geometry.make_point(max_x, min_y, min_z, 1, 0); // 6
        geometry.make_point(min_x, min_y, min_z, 0, 0); // 7

        geometry.make_polygon( {0, 1, 2, 3} );
        geometry.make_polygon( {0, 3, 7, 4} );
        geometry.make_polygon( {0, 4, 5, 1} ); // top
        geometry.make_polygon( {5, 4, 7, 6} );
        geometry.make_polygon( {2, 1, 5, 6} );
        geometry.make_polygon( {7, 3, 2, 6} ); // bottom

        geometry.make_point_corners();
        geometry.build_edges();
        geometry.compute_polygon_normals();
        geometry.compute_polygon_centroids();
    });
}

auto make_box(double r)
-> Geometry
{
    const double sq3 = std::sqrt(3.0);
    return make_box(2.0 * r / sq3, 2.0 * r / sq3, 2.0 * r / sq3);
}

struct Box_builder
{
    Geometry& geometry;
    vec3      size{0.0f};
    ivec3     div {0};
    float     p   {0.0};

    std::map<int, Point_id> points;

    Property_map<Point_id  , vec3>* point_locations  {nullptr};
    Property_map<Point_id  , vec3>* point_normals    {nullptr};
    Property_map<Point_id  , vec2>* point_texcoords  {nullptr};
    Property_map<Corner_id , vec3>* corner_normals   {nullptr};
    Property_map<Corner_id , vec2>* corner_texcoords {nullptr};
    Property_map<Polygon_id, vec3>* polygon_centroids{nullptr};
    Property_map<Polygon_id, vec3>* polygon_normals  {nullptr};

    auto make_point(int x, int y, int z, glm::vec3 n, float s, float t)
    -> Point_id
    {
        const int key = y * (div.x * 4 * div.z * 4) +
                        x * (div.z * 4) +
                        z;

        const auto i = points.find(key);
        if (i != points.end())
        {
            return i->second;
        }

        const float rel_x  = static_cast<float>(x) / static_cast<float>(div.x);
        const float rel_y  = static_cast<float>(y) / static_cast<float>(div.y);
        const float rel_z  = static_cast<float>(z) / static_cast<float>(div.z);
        const float rel_xp = signed_pow(rel_x, p);
        const float rel_yp = signed_pow(rel_y, p);
        const float rel_zp = signed_pow(rel_z, p);

        const float x_p = rel_xp * size.x;
        const float y_p = rel_yp * size.y;
        const float z_p = rel_zp * size.z;

        const Point_id point_id         = geometry.make_point();
        const bool     is_discontinuity = (x == -div.x) || (x == div.x) ||
                                          (y == -div.y) || (y == div.y) ||
                                          (z == -div.z) || (z == div.z);

        point_locations->put(point_id, vec3(x_p, y_p, z_p));
        if (!is_discontinuity)
        {
            point_normals->put(point_id, n);
            point_texcoords->put(point_id, vec2(s, t));
        }

        points[key] = point_id;

        return point_id;
    }

    auto make_corner(Polygon_id polygon_id, int x, int y, int z, glm::vec3 n, float s, float t)
    -> Corner_id
    {
        const int key = y * (div.x * 4 * div.z * 4) +
                        x * (div.z * 4) +
                        z;

        const Point_id  point_id         = points[key];
        const Corner_id corner_id        = geometry.make_polygon_corner(polygon_id, point_id);
        const bool      is_discontinuity = (x == -div.x) || (x == div.x) ||
                                           (y == -div.y) || (y == div.y) ||
                                           (z == -div.z) || (z == div.z);
        if (is_discontinuity)
        {
            corner_normals->put(corner_id, n);
            corner_texcoords->put(corner_id, vec2(s, t));
        }

        return corner_id;
    }

    Box_builder(Geometry& geometry, vec3 size, ivec3 div, float p)
        : geometry{geometry}
        , size    {size}
        , div     {div}
        , p       {p}
    {
        point_locations   = geometry.point_attributes()  .create<vec3>(c_point_locations  );
        point_normals     = geometry.point_attributes()  .create<vec3>(c_point_normals    );
        point_texcoords   = geometry.point_attributes()  .create<vec2>(c_point_texcoords  );
        corner_normals    = geometry.corner_attributes() .create<vec3>(c_corner_normals   );
        corner_texcoords  = geometry.corner_attributes() .create<vec2>(c_corner_texcoords );
        polygon_centroids = geometry.polygon_attributes().create<vec3>(c_polygon_centroids);
        polygon_normals   = geometry.polygon_attributes().create<vec3>(c_polygon_normals  );
    }

    void build()
    {
        int x;
        int y;
        int z;

        // Generate vertices
        constexpr const vec3 unit_x(1.0f, 0.0f, 0.0f);
        constexpr const vec3 unit_y(0.0f, 1.0f, 0.0f);
        constexpr const vec3 unit_z(0.0f, 0.0f, 1.0f);
        for (x = -div.x; x <= div.x; x++)
        {
            const float rel_x = 0.5f + static_cast<float>(x) / size.x;
            for (z = -div.z; z <= div.z; z++)
            {
                const float rel_z = 0.5f + static_cast<float>(z) / size.z;
                make_point(x,  div.y, z,  unit_y, rel_x, rel_z);
                make_point(x, -div.y, z, -unit_y, rel_x, rel_z);
            }
            for (y = -div.y; y <= div.y; y++)
            {
                const float rel_y = 0.5f + static_cast<float>(y) / size.y;
                make_point(x, y,  div.z,  unit_z, rel_x, rel_y);
                make_point(x, y, -div.z, -unit_z, rel_x, rel_y);
            }
        }

        // Left and right
        for (z = -div.z; z <= div.z; z++)
        {
            const float rel_z = 0.5f + static_cast<float>(z) / size.z;
            for (y = -div.y; y <= div.y; y++)
            {
                const float rel_y = 0.5f + static_cast<float>(y) / size.y;
                make_point( div.x, y, z,  unit_x, rel_y, rel_z);
                make_point(-div.x, y, z, -unit_x, rel_y, rel_z);
            }
        }

        // Generate quads
        for (x = -div.x; x < div.x; x++)
        {
            const float rel_x1 = 0.5f + static_cast<float>(x) / size.x;
            const float rel_x2 = 0.5f + static_cast<float>(x + 1) / size.x;
            for (z = -div.z; z < div.z; z++)
            {
                const float rel_z1 = 0.5f + static_cast<float>(z) / size.z;
                const float rel_z2 = 0.5f + static_cast<float>(z + 1) / size.z;

                const Polygon_id top_id = geometry.make_polygon();
                make_corner(top_id, x + 1, div.y, z,     unit_y, rel_x2, rel_z1);
                make_corner(top_id, x + 1, div.y, z + 1, unit_y, rel_x2, rel_z2);
                make_corner(top_id, x,     div.y, z + 1, unit_y, rel_x1, rel_z2);
                make_corner(top_id, x,     div.y, z,     unit_y, rel_x1, rel_z1);

                const Polygon_id bottom_id = geometry.make_polygon();
                make_corner(bottom_id, x,     -div.y, z,     -unit_y, rel_x1, rel_z1);
                make_corner(bottom_id, x,     -div.y, z + 1, -unit_y, rel_x1, rel_z2);
                make_corner(bottom_id, x + 1, -div.y, z + 1, -unit_y, rel_x2, rel_z2);
                make_corner(bottom_id, x + 1, -div.y, z,     -unit_y, rel_x2, rel_z1);

                polygon_normals->put(top_id, unit_y);
                polygon_normals->put(bottom_id, -unit_y);
            }
            for (y = -div.y; y < div.y; y++)
            {
                const float rel_y1 = 0.5f + static_cast<float>(y    ) / size.y;
                const float rel_y2 = 0.5f + static_cast<float>(y + 1) / size.y;

                const Polygon_id back_id = geometry.make_polygon();
                make_corner(back_id, x,     y,     div.z, unit_z, rel_x1, rel_y1);
                make_corner(back_id, x,     y + 1, div.z, unit_z, rel_x1, rel_y2);
                make_corner(back_id, x + 1, y + 1, div.z, unit_z, rel_x2, rel_y2);
                make_corner(back_id, x + 1, y,     div.z, unit_z, rel_x2, rel_y1);

                const Polygon_id front_id = geometry.make_polygon();
                make_corner(front_id, x + 1, y,     -div.z, -unit_z, rel_x2, rel_y1);
                make_corner(front_id, x + 1, y + 1, -div.z, -unit_z, rel_x2, rel_y2);
                make_corner(front_id, x,     y + 1, -div.z, -unit_z, rel_x1, rel_y2);
                make_corner(front_id, x,     y,     -div.z, -unit_z, rel_x1, rel_y1);

                polygon_normals->put(back_id, unit_z);
                polygon_normals->put(front_id, -unit_z);
            }
        }

        for (z = -div.z; z < div.z; z++)
        {
            const float rel_z1 = 0.5f + static_cast<float>(z    ) / size.z;
            const float rel_z2 = 0.5f + static_cast<float>(z + 1) / size.z;

            for (y = -div.y; y < div.y; y++)
            {
                const float rel_y1 = 0.5f + static_cast<float>(y    ) / size.y;
                const float rel_y2 = 0.5f + static_cast<float>(y + 1) / size.y;

                const Polygon_id right_id = geometry.make_polygon();
                make_corner(right_id, div.x, y,     z,     unit_x, rel_y1, rel_z1);
                make_corner(right_id, div.x, y,     z + 1, unit_x, rel_y1, rel_z2);
                make_corner(right_id, div.x, y + 1, z + 1, unit_x, rel_y2, rel_z2);
                make_corner(right_id, div.x, y + 1, z,     unit_x, rel_y2, rel_z1);

                const Polygon_id left_id = geometry.make_polygon();
                make_corner(left_id, -div.x, y + 1, z,     -unit_x, rel_y2, rel_z1);
                make_corner(left_id, -div.x, y + 1, z + 1, -unit_x, rel_y2, rel_z2);
                make_corner(left_id, -div.x, y,     z + 1, -unit_x, rel_y1, rel_z2);
                make_corner(left_id, -div.x, y,     z,     -unit_x, rel_y1, rel_z1);

                polygon_normals->put(right_id, unit_x);
                polygon_normals->put(left_id, -unit_x);
            }
        }

        geometry.make_point_corners();
        geometry.build_edges();
        geometry.compute_polygon_normals();
        geometry.compute_polygon_centroids();
    }
};

auto make_box(glm::vec3 size, glm::ivec3 div, float p)
-> Geometry
{
    ZoneScoped;

    return Geometry("box", [size, div, p](auto& geometry){
        Box_builder builder(geometry, size / 2.0f, div, p);
        builder.build();
    });
}

} // namespace erhe::geometry::shapes
