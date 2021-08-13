#include "erhe/geometry/shapes/regular_polygon.hpp"
#include "Tracy.hpp"

#include <cmath>  // for sqrt

namespace erhe::geometry::shapes
{

auto make_triangle(const double r)
-> Geometry
{
    ZoneScoped;

    // 0.57735027 = sqrt(3) / 3
    // 0.28867513 = sqrt(3) / 6
    return Geometry("triangle", [=](auto& geometry) {
        geometry.make_point(static_cast<float>(r * -0.28867513f), static_cast<float>(r *  0.5f), 0.0f, 0.0f, 1.0f);
        geometry.make_point(static_cast<float>(r *  0.57735027f), static_cast<float>(r *  0.0f), 0.0f, 1.0f, 1.0f);
        geometry.make_point(static_cast<float>(r * -0.28867513f), static_cast<float>(r * -0.5f), 0.0f, 1.0f, 0.0f);

        geometry.make_polygon( {0, 1, 2} );

        geometry.make_point_corners();
        geometry.build_edges();
    });
}

auto make_quad(const double edge)
-> Geometry
{
    ZoneScoped;

    //
    //  0.707106781 = sqrt(2) / 2
    // radius version:
    // make_point((float)(r * -0.707106781f), (float)(r * -0.707106781f), 0.0f, 0.0f, 0.0f);
    // make_point((float)(r *  0.707106781f), (float)(r * -0.707106781f), 0.0f, 1.0f, 0.0f);
    // make_point((float)(r *  0.707106781f), (float)(r *  0.707106781f), 0.0f, 1.0f, 1.0f);
    // make_point((float)(r * -0.707106781f), (float)(r *  0.707106781f), 0.0f, 0.0f, 1.0f);
    return Geometry("quad", [=](auto& geometry) {
        geometry.make_point(static_cast<float>(edge * -0.5f), static_cast<float>(edge * -0.5f), 0.0f, 0.0f, 0.0f);
        geometry.make_point(static_cast<float>(edge *  0.5f), static_cast<float>(edge * -0.5f), 0.0f, 1.0f, 0.0f);
        geometry.make_point(static_cast<float>(edge *  0.5f), static_cast<float>(edge *  0.5f), 0.0f, 1.0f, 1.0f);
        geometry.make_point(static_cast<float>(edge * -0.5f), static_cast<float>(edge *  0.5f), 0.0f, 0.0f, 1.0f);

        geometry.make_polygon( {0, 1, 2, 3} );

        // Double sided
        geometry.make_polygon( {3, 2, 1, 0} );

        geometry.make_point_corners();
        geometry.build_edges();
    });
}

} // namespace erhe::geometry::shapes
