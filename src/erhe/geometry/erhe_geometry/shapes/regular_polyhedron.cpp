#include "erhe_geometry/shapes/regular_polyhedron.hpp"
#include "erhe_profile/profile.hpp"

#include <cmath>  // for sqrt

namespace erhe::geometry::shapes
{

auto make_cuboctahedron(const double r) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    return Geometry{
        "cuboctahedron",
        [=](auto& geometry) {
            const double sq2 = std::sqrt(2.0);

            geometry.make_point( 0,      r,      0          );
            geometry.make_point( r / 2,  r / 2,  r * sq2 / 2);
            geometry.make_point( r / 2,  r / 2, -r * sq2 / 2);
            geometry.make_point( r,      0,      0          );
            geometry.make_point( r / 2, -r / 2,  r * sq2 / 2);
            geometry.make_point( r / 2, -r / 2, -r * sq2 / 2);
            geometry.make_point(     0, -r,      0          );
            geometry.make_point(-r / 2, -r / 2,  r * sq2 / 2);
            geometry.make_point(-r / 2, -r / 2, -r * sq2 / 2);
            geometry.make_point(-r,          0,  0          );
            geometry.make_point(-r / 2,  r / 2,  r * sq2 / 2);
            geometry.make_point(-r / 2,  r / 2, -r * sq2 / 2);

            geometry.make_polygon_reverse( { 1, 4,  7, 10} );
            geometry.make_polygon_reverse( { 4, 3,  5,  6} );
            geometry.make_polygon_reverse( { 0, 2,  3,  1} );
            geometry.make_polygon_reverse( {11, 8,  5,  2} );
            geometry.make_polygon_reverse( {10, 9, 11,  0} );
            geometry.make_polygon_reverse( { 7, 6,  8,  9} );
            geometry.make_polygon_reverse( { 0, 1, 10} );
            geometry.make_polygon_reverse( { 3, 4,  1} );
            geometry.make_polygon_reverse( { 4, 6,  7} );
            geometry.make_polygon_reverse( {10, 7,  9} );
            geometry.make_polygon_reverse( {11, 2,  0} );
            geometry.make_polygon_reverse( { 2, 5,  3} );
            geometry.make_polygon_reverse( { 8, 6,  5} );
            geometry.make_polygon_reverse( { 9, 8, 11} );

            geometry.make_point_corners();
            geometry.build_edges();
            geometry.generate_polygon_texture_coordinates(true);
            geometry.compute_tangents(true, true, false, false, true, true);
        }
    };
}

auto make_dodecahedron(const double r) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    return Geometry{
        "dodecahedron",
        [=](auto& geometry) {
            const double sq3 = std::sqrt(3.0);
            const double sq5 = std::sqrt(5.0);
            const double a   = 2.0 / (sq3 + sq3 * sq5);
            const double b   = 1.0 / (3.0 * a);

            geometry.make_point( r / sq3,  r / sq3,  r / sq3);
            geometry.make_point( r / sq3,  r / sq3, -r / sq3);
            geometry.make_point( r / sq3, -r / sq3,  r / sq3);
            geometry.make_point( r / sq3, -r / sq3, -r / sq3);
            geometry.make_point(-r / sq3,  r / sq3,  r / sq3);
            geometry.make_point(-r / sq3,  r / sq3, -r / sq3);
            geometry.make_point(-r / sq3, -r / sq3,  r / sq3);
            geometry.make_point(-r / sq3, -r / sq3, -r / sq3);
            geometry.make_point( 0,        r * a,    r * b  );
            geometry.make_point( 0,        r * a,   -r * b  );
            geometry.make_point( 0,       -r * a,    r * b  );
            geometry.make_point( 0,       -r * a,   -r * b  );
            geometry.make_point( r * a,    r * b,    0      );
            geometry.make_point( r * a,   -r * b,    0      );
            geometry.make_point(-r * a,    r * b,    0      );
            geometry.make_point(-r * a,   -r * b,    0      );
            geometry.make_point( r * b,    0,        r * a  );
            geometry.make_point( r * b,    0,       -r * a  );
            geometry.make_point(-r * b,    0,        r * a  );
            geometry.make_point(-r * b,    0,       -r * a  );

            geometry.make_polygon_reverse( { 6, 18, 4,  8, 10} );
            geometry.make_polygon_reverse( {10,  8, 0, 16,  2} );
            geometry.make_polygon_reverse( { 3, 17, 1,  9, 11} );
            geometry.make_polygon_reverse( { 5, 19, 7, 11,  9} );
            geometry.make_polygon_reverse( { 3, 13, 2, 16, 17} );
            geometry.make_polygon_reverse( {17, 16, 0, 12,  1} );
            geometry.make_polygon_reverse( {19, 18, 6, 15,  7} );
            geometry.make_polygon_reverse( { 5, 14, 4, 18, 19} );
            geometry.make_polygon_reverse( { 0,  8, 4, 14, 12} );
            geometry.make_polygon_reverse( {12, 14, 5,  9,  1} );
            geometry.make_polygon_reverse( {13, 15, 6, 10,  2} );
            geometry.make_polygon_reverse( { 3, 11, 7, 15, 13} );

            geometry.make_point_corners();
            geometry.build_edges();
            geometry.generate_polygon_texture_coordinates(true);
            geometry.compute_tangents(true, true, false, false, true, true);
        }
    };
}

auto make_icosahedron(const double r) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    return Geometry{
        "icosahedron",
        [=](auto& geometry) {
            const double sq5 = std::sqrt(5.0);
            const double a0  = 2.0 / (1.0 + sq5);
            const double b   = std::sqrt((3.0 + sq5) / (1.0 + sq5));
            const double a   = a0 / b;

            geometry.make_point( 0,      r * a,  r / b);
            geometry.make_point( 0,      r * a, -r / b);
            geometry.make_point( 0,     -r * a,  r / b);
            geometry.make_point( 0,     -r * a, -r / b);
            geometry.make_point( r * a,  r / b,  0    );
            geometry.make_point( r * a, -r / b,  0    );
            geometry.make_point(-r * a,  r / b,  0    );
            geometry.make_point(-r * a, -r / b,  0    );
            geometry.make_point( r / b,  0,      r * a);
            geometry.make_point( r / b,  0,     -r * a);
            geometry.make_point(-r / b,  0,      r * a);
            geometry.make_point(-r / b,  0,     -r * a);

            geometry.make_polygon_reverse( {1,  4,  6} );
            geometry.make_polygon_reverse( {0,  6,  4} );
            geometry.make_polygon_reverse( {0,  2, 10} );
            geometry.make_polygon_reverse( {0,  8,  2} );
            geometry.make_polygon_reverse( {1,  3,  9} );
            geometry.make_polygon_reverse( {1, 11,  3} );
            geometry.make_polygon_reverse( {2,  5,  7} );
            geometry.make_polygon_reverse( {3,  7,  5} );
            geometry.make_polygon_reverse( {6, 10, 11} );
            geometry.make_polygon_reverse( {7, 11, 10} );
            geometry.make_polygon_reverse( {4,  9,  8} );
            geometry.make_polygon_reverse( {5,  8,  9} );
            geometry.make_polygon_reverse( {0, 10,  6} );
            geometry.make_polygon_reverse( {0,  4,  8} );
            geometry.make_polygon_reverse( {1,  6, 11} );
            geometry.make_polygon_reverse( {1,  9,  4} );
            geometry.make_polygon_reverse( {3, 11,  7} );
            geometry.make_polygon_reverse( {3,  5,  9} );
            geometry.make_polygon_reverse( {2,  7, 10} );
            geometry.make_polygon_reverse( {2,  8,  5} );

            geometry.make_point_corners();
            geometry.build_edges();
            geometry.generate_polygon_texture_coordinates(true);
            geometry.compute_tangents(true, true, false, false, true, true);
        }
    };
}

auto make_octahedron(const double r) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    return Geometry{
        "octahedron",
        [=](auto& geometry) {
            geometry.make_point( 0,  r,  0);
            geometry.make_point( 0, -r,  0);
            geometry.make_point(-r,  0,  0);
            geometry.make_point( 0,  0, -r);
            geometry.make_point( r,  0,  0);
            geometry.make_point( 0,  0,  r);

            geometry.make_polygon_reverse( {0, 2, 3} );
            geometry.make_polygon_reverse( {0, 3, 4} );
            geometry.make_polygon_reverse( {0, 4, 5} );
            geometry.make_polygon_reverse( {0, 5, 2} );
            geometry.make_polygon_reverse( {3, 2, 1} );
            geometry.make_polygon_reverse( {4, 3, 1} );
            geometry.make_polygon_reverse( {5, 4, 1} );
            geometry.make_polygon_reverse( {2, 5, 1} );

            geometry.make_point_corners();
            geometry.build_edges();
            geometry.generate_polygon_texture_coordinates(true);
            geometry.compute_tangents(true, true, false, false, true, true);
        }
    };
}

auto make_tetrahedron(double r) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    return Geometry{
        "tetrahedron",
        [=](auto& geometry) {
            const double sq2 = std::sqrt(2.0);
            const double sq3 = std::sqrt(3.0);

            geometry.make_point( 0,                    r,        0                  );
            geometry.make_point( 0,                   -r / 3.0,  r * 2.0 * sq2 / 3.0);
            geometry.make_point(-r * sq3 * sq2 / 3.0, -r / 3.0, -r * sq2 / 3.0      );
            geometry.make_point( r * sq3 * sq2 / 3.0, -r / 3.0, -r * sq2 / 3.0      );

            geometry.make_polygon_reverse( {0, 1, 2} );
            geometry.make_polygon_reverse( {3, 1, 0} );
            geometry.make_polygon_reverse( {0, 2, 3} );
            geometry.make_polygon_reverse( {3, 2, 1} );

            geometry.make_point_corners();
            geometry.build_edges();
            geometry.generate_polygon_texture_coordinates(true);
            geometry.compute_tangents(true, true, false, false, true, true);
        }
    };
}

auto make_cube(const double r) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    return Geometry{
        "cube",
        [=](Geometry& geometry) {
            const double a =  0.5 * r;
            const double b = -0.5 * r;
            Property_map<Corner_id, glm::vec2>* corner_texcoords = geometry.corner_attributes().create<glm::vec2>(c_corner_texcoords);

            geometry.make_point(b, b, b); // 0    2------4
            geometry.make_point(a, b, b); // 1   /|     /|
            geometry.make_point(b, a, b); // 2  6-+----7 |
            geometry.make_point(b, b, a); // 3  | |    | |
            geometry.make_point(a, a, b); // 4  | |    | |
            geometry.make_point(a, b, a); // 5  | 0----|-1
            geometry.make_point(b, a, a); // 6  |/     |/
            geometry.make_point(a, a, a); // 7  3------5

            geometry.make_polygon({1, 4, 7, 5}); // x+
            geometry.make_polygon({2, 6, 7, 4}); // y+
            geometry.make_polygon({3, 5, 7, 6}); // z+
            geometry.make_polygon({0, 3, 6, 2}); // x-
            geometry.make_polygon({0, 1, 5, 3}); // y-
            geometry.make_polygon({0, 2, 4, 1}); // z-

            geometry.make_point_corners();
            geometry.build_edges();
            geometry.compute_polygon_corner_texcoords(corner_texcoords);
            //geometry.generate_polygon_texture_coordinates(true);
            geometry.compute_polygon_normals();
            geometry.compute_polygon_centroids();
            geometry.compute_tangents(true, true, false, false, true, true);
        }
    };
}

} // namespace erhe::geometry::shapes
