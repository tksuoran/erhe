#include "erhe/geometry/shapes/regular_polyhedron.hpp"
#include "Tracy.hpp"
#include <cmath>  // for sqrt

namespace erhe::geometry::shapes
{

auto make_cuboctahedron(double r)
-> Geometry
{
    ZoneScoped;

    double sq2 = std::sqrt(2.0);

    Geometry geometry("cuboctahedron");

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

    geometry.make_polygon( { 1, 4,  7, 10} );
    geometry.make_polygon( { 4, 3,  5,  6} );
    geometry.make_polygon( { 0, 2,  3,  1} );
    geometry.make_polygon( {11, 8,  5,  2} );
    geometry.make_polygon( {10, 9, 11,  0} );
    geometry.make_polygon( { 7, 6,  8,  9} );
    geometry.make_polygon( { 0, 1, 10} );
    geometry.make_polygon( { 3, 4,  1} );
    geometry.make_polygon( { 4, 6,  7} );
    geometry.make_polygon( {10, 7,  9} );
    geometry.make_polygon( {11, 2,  0} );
    geometry.make_polygon( { 2, 5,  3} );
    geometry.make_polygon( { 8, 6,  5} );
    geometry.make_polygon( { 9, 8, 11} );

    geometry.make_point_corners();
    geometry.build_edges();
    geometry.generate_polygon_texture_coordinates(true);
    geometry.compute_tangents(true, true, false, false, true, true);
    return geometry;
}

auto make_dodecahedron(double r)
-> Geometry
{
    ZoneScoped;

    double sq3 = std::sqrt(3.0);
    double sq5 = std::sqrt(5.0);
    double a   = 2.0 / (sq3 + sq3 * sq5);
    double b   = 1.0 / (3.0 * a);

    Geometry geometry("dodecahedron");

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

    geometry.make_polygon( { 6, 18, 4,  8, 10} );
    geometry.make_polygon( {10,  8, 0, 16,  2} );
    geometry.make_polygon( { 3, 17, 1,  9, 11} );
    geometry.make_polygon( { 5, 19, 7, 11,  9} );
    geometry.make_polygon( { 3, 13, 2, 16, 17} );
    geometry.make_polygon( {17, 16, 0, 12,  1} );
    geometry.make_polygon( {19, 18, 6, 15,  7} );
    geometry.make_polygon( { 5, 14, 4, 18, 19} );
    geometry.make_polygon( { 0,  8, 4, 14, 12} );
    geometry.make_polygon( {12, 14, 5,  9,  1} );
    geometry.make_polygon( {13, 15, 6, 10,  2} );
    geometry.make_polygon( { 3, 11, 7, 15, 13} );

    geometry.make_point_corners();
    geometry.build_edges();
    geometry.generate_polygon_texture_coordinates(true);
    geometry.compute_tangents(true, true, false, false, true, true);
    return geometry;
}

auto make_icosahedron(double r)
-> Geometry
{
    ZoneScoped;

    double sq5 = std::sqrt(5.0);
    double a   = 2.0 / (1.0 + sq5);
    double b   = std::sqrt((3.0 + sq5) / (1.0 + sq5));
    a /= b;

    Geometry geometry("icosahedron");

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

    geometry.make_polygon( {1,  4,  6} );
    geometry.make_polygon( {0,  6,  4} );
    geometry.make_polygon( {0,  2, 10} );
    geometry.make_polygon( {0,  8,  2} );
    geometry.make_polygon( {1,  3,  9} );
    geometry.make_polygon( {1, 11,  3} );
    geometry.make_polygon( {2,  5,  7} );
    geometry.make_polygon( {3,  7,  5} );
    geometry.make_polygon( {6, 10, 11} );
    geometry.make_polygon( {7, 11, 10} );
    geometry.make_polygon( {4,  9,  8} );
    geometry.make_polygon( {5,  8,  9} );
    geometry.make_polygon( {0, 10,  6} );
    geometry.make_polygon( {0,  4,  8} );
    geometry.make_polygon( {1,  6, 11} );
    geometry.make_polygon( {1,  9,  4} );
    geometry.make_polygon( {3, 11,  7} );
    geometry.make_polygon( {3,  5,  9} );
    geometry.make_polygon( {2,  7, 10} );
    geometry.make_polygon( {2,  8,  5} );

    geometry.make_point_corners();
    geometry.build_edges();
    geometry.generate_polygon_texture_coordinates(true);
    geometry.compute_tangents(true, true, false, false, true, true);
    return geometry;
}

auto make_octahedron(double r)
-> Geometry
{
    ZoneScoped;

    Geometry geometry("octahedron");

    geometry.make_point( 0,  r,  0);
    geometry.make_point( 0, -r,  0);
    geometry.make_point(-r,  0,  0);
    geometry.make_point( 0,  0, -r);
    geometry.make_point( r,  0,  0);
    geometry.make_point( 0,  0,  r);

    geometry.make_polygon( {0, 2, 3} );
    geometry.make_polygon( {0, 3, 4} );
    geometry.make_polygon( {0, 4, 5} );
    geometry.make_polygon( {0, 5, 2} );
    geometry.make_polygon( {3, 2, 1} );
    geometry.make_polygon( {4, 3, 1} );
    geometry.make_polygon( {5, 4, 1} );
    geometry.make_polygon( {2, 5, 1} );

    geometry.make_point_corners();
    geometry.build_edges();
    geometry.generate_polygon_texture_coordinates(true);
    geometry.compute_tangents(true, true, false, false, true, true);
    return geometry;
}

auto make_tetrahedron(double r)
-> Geometry
{
    ZoneScoped;

    double sq2 = std::sqrt(2.0);
    double sq3 = std::sqrt(3.0);

    Geometry geometry("tetrahedron");

    geometry.make_point( 0,                    r,        0                  );
    geometry.make_point( 0,                   -r / 3.0,  r * 2.0 * sq2 / 3.0);
    geometry.make_point(-r * sq3 * sq2 / 3.0, -r / 3.0, -r * sq2 / 3.0      );
    geometry.make_point( r * sq3 * sq2 / 3.0, -r / 3.0, -r * sq2 / 3.0      );

    geometry.make_polygon( {0, 1, 2} );
    geometry.make_polygon( {3, 1, 0} );
    geometry.make_polygon( {0, 2, 3} );
    geometry.make_polygon( {3, 2, 1} );

    geometry.make_point_corners();
    geometry.build_edges();
    geometry.generate_polygon_texture_coordinates(true);
    geometry.compute_tangents(true, true, false, false, true, true);
    return geometry;
}

auto make_cube(double r)
-> Geometry
{
    ZoneScoped;

    Geometry geometry("cube");

    double a =  0.5 * r;
    double b = -0.5 * r;                  
    geometry.make_point(b, b, b); // 0    6------7  
    geometry.make_point(a, b, b); // 1   /|     /|  
    geometry.make_point(b, a, b); // 2  2-+----4 |  
    geometry.make_point(b, b, a); // 3  | |    | |  
    geometry.make_point(a, a, b); // 4  | |    | |  
    geometry.make_point(a, b, a); // 5  | 3----+-5  
    geometry.make_point(b, a, a); // 6  |/     |/   
    geometry.make_point(a, a, a); // 7  0------1    
    geometry.make_polygon( {0, 1, 4, 2} ); // z min
    geometry.make_polygon( {0, 3, 5, 1} ); // y min bottom
    geometry.make_polygon( {0, 2, 6, 3} ); // x min
    geometry.make_polygon( {7, 5, 3, 6} ); // z max
    geometry.make_polygon( {7, 4, 1, 5} ); // x max
    geometry.make_polygon( {6, 2, 4, 7} ); // y max top

    geometry.make_point_corners();
    geometry.build_edges();
    geometry.generate_polygon_texture_coordinates(true);
    geometry.compute_tangents(true, true, false, false, true, true);
    return geometry;
}

} // namespace erhe::geometry::shapes
