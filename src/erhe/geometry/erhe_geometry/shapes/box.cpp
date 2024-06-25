#include "erhe_geometry/shapes/box.hpp"
#include "erhe_profile/profile.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <map>
#include <memory>

namespace erhe::geometry::shapes
{

using vec2  = glm::vec2;
using vec3  = glm::vec3;
using ivec3 = glm::ivec3;
using vec4  = glm::vec4;

namespace
{

auto sign(const float x) -> float
{
    return x < 0.0f ? -1.0f : 1.0f;
}

auto signed_pow(const float x, const float p) -> float
{
    return sign(x) * std::pow(std::abs(x), p);
}

} // namespace

auto make_box(
    const double x_size,
    const double y_size,
    const double z_size
) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    const double x = x_size / 2.0;
    const double y = y_size / 2.0;
    const double z = z_size / 2.0;

    return Geometry{
        "box",
        [=](Geometry& geometry) {
            Property_map<Corner_id, vec2>* corner_texcoords = geometry.corner_attributes().create<vec2>(c_corner_texcoords);

            geometry.make_point(-x, -y, -z); // 0    2------4
            geometry.make_point( x, -y, -z); // 1   /|     /|
            geometry.make_point(-x,  y, -z); // 2  6-+----7 |
            geometry.make_point(-x, -y,  z); // 3  | |    | |
            geometry.make_point( x,  y, -z); // 4  | |    | |
            geometry.make_point( x, -y,  z); // 5  | 0----|-1
            geometry.make_point(-x,  y,  z); // 6  |/     |/
            geometry.make_point( x,  y,  z); // 7  3------5

            geometry.make_polygon({1, 4, 7, 5}); // x+
            geometry.make_polygon({2, 6, 7, 4}); // y+
            geometry.make_polygon({3, 5, 7, 6}); // z+
            geometry.make_polygon({0, 3, 6, 2}); // x-
            geometry.make_polygon({0, 1, 5, 3}); // y-
            geometry.make_polygon({0, 2, 4, 1}); // z-

            geometry.reverse_polygons(); // TODO reverse the code above and remove this
            geometry.make_point_corners();
            geometry.build_edges();
            geometry.compute_polygon_corner_texcoords(corner_texcoords);
            geometry.compute_polygon_normals();
            geometry.compute_polygon_centroids();
            geometry.compute_tangents(true, true, false, false, true, true);
        }
    };
}

auto make_box(
    const float min_x,
    const float max_x,
    const float min_y,
    const float max_y,
    const float min_z,
    const float max_z
) -> Geometry
{
    return Geometry{
        "box",
        [=](auto& geometry) {
            Property_map<Corner_id, vec2>* corner_texcoords = geometry.corner_attributes(). template create<vec2>(c_corner_texcoords);

            geometry.make_point(min_x, min_y, min_z); // 0    2------4
            geometry.make_point(max_x, min_y, min_z); // 1   /|     /|
            geometry.make_point(min_x, max_y, min_z); // 2  6-+----7 |
            geometry.make_point(min_x, min_y, max_z); // 3  | |    | |
            geometry.make_point(max_x, max_y, min_z); // 4  | |    | |
            geometry.make_point(max_x, min_y, max_z); // 5  | 0----|-1
            geometry.make_point(min_x, max_y, max_z); // 6  |/     |/
            geometry.make_point(max_x, max_y, max_z); // 7  3------5

            geometry.make_polygon({1, 4, 7, 5}); // x+
            geometry.make_polygon({2, 6, 7, 4}); // y+
            geometry.make_polygon({3, 5, 7, 6}); // z+
            geometry.make_polygon({0, 3, 6, 2}); // x-
            geometry.make_polygon({0, 1, 5, 3}); // y-
            geometry.make_polygon({0, 2, 4, 1}); // z-

            geometry.reverse_polygons(); // TODO reverse the code above and remove this
            geometry.make_point_corners();
            geometry.build_edges();
            geometry.compute_polygon_corner_texcoords(corner_texcoords);
            geometry.compute_polygon_normals();
            geometry.compute_polygon_centroids();
            geometry.compute_tangents(true, true, false, false, true, true);
        }
    };
}

auto make_box(const double r) -> Geometry
{
    const double sq3 = std::sqrt(3.0);
    return make_box(2.0 * r / sq3, 2.0 * r / sq3, 2.0 * r / sq3);
}

class Box_builder
{
public:
    Geometry& geometry;
    vec3      size{0.0f};
    ivec3     div {0};
    float     p   {0.0};

    std::map<int, Point_id> points;

    Property_map<Point_id  , vec3>* point_locations  {nullptr};
    Property_map<Point_id  , vec3>* point_normals    {nullptr};
    Property_map<Point_id  , vec2>* point_texcoords  {nullptr};
    Property_map<Point_id  , vec4>* point_tangents   {nullptr};
    Property_map<Point_id  , vec4>* point_bitangents {nullptr};
    Property_map<Corner_id , vec3>* corner_normals   {nullptr};
    Property_map<Corner_id , vec2>* corner_texcoords {nullptr};
    Property_map<Corner_id , vec4>* corner_tangents  {nullptr};
    Property_map<Corner_id , vec4>* corner_bitangents{nullptr};
    Property_map<Polygon_id, vec3>* polygon_centroids{nullptr};
    Property_map<Polygon_id, vec3>* polygon_normals  {nullptr};

    void ortho_basis_pixar_r1(const vec3 N, vec4& T, vec4& B)
    {
        const float sz = sign(N.z);
        const float a  = 1.0f / (sz + N.z);
        const float sx = sz * N.x;
        const float b  = N.x * N.y * a;
        const vec3  t_ = vec3{sx * N.x * a - 1.f, sz * b, sx};
        const vec3  b_ = vec3{b, N.y * N.y * a - sz, N.y};

        const vec3  t_xyz = glm::normalize(t_ - N * glm::dot(N, t_));
        const float t_w   = (glm::dot(glm::cross(N, t_), b_) < 0.0f) ? -1.0f : 1.0f;
        const vec3  b_xyz = glm::normalize(b_ - N * glm::dot(N, b_));
        const float b_w   = (glm::dot(glm::cross(b_, N), t_) < 0.0f) ? -1.0f : 1.0f;
        T = vec4{t_xyz, t_w};
        B = vec4{b_xyz, b_w};
    }

    auto make_point(
        const int       x,
        const int       y,
        const int       z,
        const glm::vec3 n,
        const float     s,
        const float     t
    ) -> Point_id
    {
        const int key =
            y * (div.x * 4 * div.z * 4) +
            x * (div.z * 4) +
            z;

        const auto i = points.find(key);
        if (i != points.end()) {
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
        const bool     is_discontinuity =
            (x == -div.x) || (x == div.x) ||
            (y == -div.y) || (y == div.y) ||
            (z == -div.z) || (z == div.z);

        point_locations->put(point_id, vec3{x_p, y_p, z_p});
        if (!is_discontinuity) {
            point_normals->put(point_id, n);
            point_texcoords->put(point_id, vec2{s, t});

            vec4 T;
            vec4 B;
            ortho_basis_pixar_r1(n, T, B);
            point_tangents  ->put(point_id, T);
            point_bitangents->put(point_id, B);
        }

        points[key] = point_id;

        return point_id;
    }

    auto make_corner(
        const Polygon_id polygon_id,
        const int        x,
        const int        y,
        const int        z,
        const glm::vec3  n,
        const float      s,
        const float      t
    ) -> Corner_id
    {
        const int key =
            y * (div.x * 4 * div.z * 4) +
            x * (div.z * 4) +
            z;

        const Point_id  point_id         = points[key];
        const Corner_id corner_id        = geometry.make_polygon_corner(polygon_id, point_id);
        const bool      is_discontinuity =
            (x == -div.x) || (x == div.x) ||
            (y == -div.y) || (y == div.y) ||
            (z == -div.z) || (z == div.z);
        if (is_discontinuity) {
            corner_normals->put(corner_id, n);
            corner_texcoords->put(corner_id, vec2{s, t});
            vec4 B;
            vec4 T;
            ortho_basis_pixar_r1(n, B, T);
            corner_tangents  ->put(corner_id, T);
            corner_bitangents->put(corner_id, B);
        }

        return corner_id;
    }

    Box_builder(
        Geometry&   geometry,
        const vec3  size,
        const ivec3 div,
        const float p
    )
        : geometry{geometry}
        , size    {size}
        , div     {div}
        , p       {p}
    {
        point_locations   = geometry.point_attributes  ().create<vec3>(c_point_locations  );
        point_normals     = geometry.point_attributes  ().create<vec3>(c_point_normals    );
        point_texcoords   = geometry.point_attributes  ().create<vec2>(c_point_texcoords  );
        point_tangents    = geometry.point_attributes  ().create<vec4>(c_point_tangents   );
        point_bitangents  = geometry.point_attributes  ().create<vec4>(c_point_bitangents );
        corner_normals    = geometry.corner_attributes ().create<vec3>(c_corner_normals   );
        corner_texcoords  = geometry.corner_attributes ().create<vec2>(c_corner_texcoords );
        corner_tangents   = geometry.corner_attributes ().create<vec4>(c_corner_tangents  );
        corner_bitangents = geometry.corner_attributes ().create<vec4>(c_corner_bitangents);
        polygon_centroids = geometry.polygon_attributes().create<vec3>(c_polygon_centroids);
        polygon_normals   = geometry.polygon_attributes().create<vec3>(c_polygon_normals  );
    }

    void build()
    {
        int x;
        int y;
        int z;

        // Generate vertices
        constexpr vec3 unit_x(1.0f, 0.0f, 0.0f);
        constexpr vec3 unit_y(0.0f, 1.0f, 0.0f);
        constexpr vec3 unit_z(0.0f, 0.0f, 1.0f);
        for (x = -div.x; x <= div.x; x++) {
            const float rel_x = 0.5f + static_cast<float>(x) / size.x;
            for (z = -div.z; z <= div.z; z++) {
                const float rel_z = 0.5f + static_cast<float>(z) / size.z;
                make_point(x,  div.y, z,  unit_y, rel_x, rel_z);
                make_point(x, -div.y, z, -unit_y, rel_x, rel_z);
            }
            for (y = -div.y; y <= div.y; y++) {
                const float rel_y = 0.5f + static_cast<float>(y) / size.y;
                make_point(x, y,  div.z,  unit_z, rel_x, rel_y);
                make_point(x, y, -div.z, -unit_z, rel_x, rel_y);
            }
        }

        // Left and right
        for (z = -div.z; z <= div.z; z++) {
            const float rel_z = 0.5f + static_cast<float>(z) / size.z;
            for (y = -div.y; y <= div.y; y++) {
                const float rel_y = 0.5f + static_cast<float>(y) / size.y;
                make_point( div.x, y, z,  unit_x, rel_y, rel_z);
                make_point(-div.x, y, z, -unit_x, rel_y, rel_z);
            }
        }

        // Generate quads
        for (x = -div.x; x < div.x; x++) {
            const float rel_x1 = 0.5f + static_cast<float>(x) / size.x;
            const float rel_x2 = 0.5f + static_cast<float>(x + 1) / size.x;
            for (z = -div.z; z < div.z; z++) {
                const float rel_z1 = 0.5f + static_cast<float>(z) / size.z;
                const float rel_z2 = 0.5f + static_cast<float>(z + 1) / size.z;

                const Polygon_id top_id = geometry.make_polygon();
                make_corner(top_id, x,     div.y, z,     unit_y, rel_x1, rel_z1);
                make_corner(top_id, x,     div.y, z + 1, unit_y, rel_x1, rel_z2);
                make_corner(top_id, x + 1, div.y, z + 1, unit_y, rel_x2, rel_z2);
                make_corner(top_id, x + 1, div.y, z,     unit_y, rel_x2, rel_z1);

                const Polygon_id bottom_id = geometry.make_polygon();
                make_corner(bottom_id, x + 1, -div.y, z,     -unit_y, rel_x2, rel_z1);
                make_corner(bottom_id, x + 1, -div.y, z + 1, -unit_y, rel_x2, rel_z2);
                make_corner(bottom_id, x,     -div.y, z + 1, -unit_y, rel_x1, rel_z2);
                make_corner(bottom_id, x,     -div.y, z,     -unit_y, rel_x1, rel_z1);

                polygon_normals->put(top_id, unit_y);
                polygon_normals->put(bottom_id, -unit_y);
            }
            for (y = -div.y; y < div.y; y++) {
                const float rel_y1 = 0.5f + static_cast<float>(y    ) / size.y;
                const float rel_y2 = 0.5f + static_cast<float>(y + 1) / size.y;

                const Polygon_id back_id = geometry.make_polygon();
                make_corner(back_id, x + 1, y,     div.z, unit_z, rel_x2, rel_y1);
                make_corner(back_id, x + 1, y + 1, div.z, unit_z, rel_x2, rel_y2);
                make_corner(back_id, x,     y + 1, div.z, unit_z, rel_x1, rel_y2);
                make_corner(back_id, x,     y,     div.z, unit_z, rel_x1, rel_y1);

                const Polygon_id front_id = geometry.make_polygon();
                make_corner(front_id, x,     y,     -div.z, -unit_z, rel_x1, rel_y1);
                make_corner(front_id, x,     y + 1, -div.z, -unit_z, rel_x1, rel_y2);
                make_corner(front_id, x + 1, y + 1, -div.z, -unit_z, rel_x2, rel_y2);
                make_corner(front_id, x + 1, y,     -div.z, -unit_z, rel_x2, rel_y1);

                polygon_normals->put(back_id, unit_z);
                polygon_normals->put(front_id, -unit_z);
            }
        }

        for (z = -div.z; z < div.z; z++) {
            const float rel_z1 = 0.5f + static_cast<float>(z    ) / size.z;
            const float rel_z2 = 0.5f + static_cast<float>(z + 1) / size.z;

            for (y = -div.y; y < div.y; y++) {
                const float rel_y1 = 0.5f + static_cast<float>(y    ) / size.y;
                const float rel_y2 = 0.5f + static_cast<float>(y + 1) / size.y;

                const Polygon_id right_id = geometry.make_polygon();
                make_corner(right_id, div.x, y + 1, z,     unit_x, rel_y2, rel_z1);
                make_corner(right_id, div.x, y + 1, z + 1, unit_x, rel_y2, rel_z2);
                make_corner(right_id, div.x, y,     z + 1, unit_x, rel_y1, rel_z2);
                make_corner(right_id, div.x, y,     z,     unit_x, rel_y1, rel_z1);

                const Polygon_id left_id = geometry.make_polygon();
                make_corner(left_id, -div.x, y,     z,     -unit_x, rel_y1, rel_z1);
                make_corner(left_id, -div.x, y,     z + 1, -unit_x, rel_y1, rel_z2);
                make_corner(left_id, -div.x, y + 1, z + 1, -unit_x, rel_y2, rel_z2);
                make_corner(left_id, -div.x, y + 1, z,     -unit_x, rel_y2, rel_z1);

                polygon_normals->put(right_id, unit_x);
                polygon_normals->put(left_id, -unit_x);
            }
        }

        geometry.reverse_polygons(); // TODO reverse the code above and remove this
        geometry.make_point_corners();
        geometry.build_edges();
        geometry.compute_polygon_normals();
        geometry.compute_polygon_centroids();
    }
};

auto make_box(
    const glm::vec3  size,
    const glm::ivec3 div,
    const float      p
) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    return Geometry{
        "box",
        [size, div, p](auto& geometry) {
            Box_builder builder{geometry, size / 2.0f, div, p};
            builder.build();
        }
    };
}

} // namespace erhe::geometry::shapes
