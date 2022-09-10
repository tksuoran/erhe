#include "erhe/geometry/shapes/regular_polygon.hpp"
#include "erhe/toolkit/profile.hpp"

#include <cmath>  // for sqrt

namespace erhe::geometry::shapes
{

auto make_triangle(const double r) -> Geometry
{
    ERHE_PROFILE_FUNCTION

    const double a = sqrt(3.0) / 3.0; // 0.57735027
    const double b = sqrt(3.0) / 6.0; // 0.28867513
    return Geometry{
        "triangle",
        [=](auto& geometry)
        {
            geometry.make_point(static_cast<float>(r * -b), static_cast<float>(r *  0.5), 0.0f, 0.0f, 1.0f);
            geometry.make_point(static_cast<float>(r *  a), static_cast<float>(r *  0.0), 0.0f, 1.0f, 1.0f);
            geometry.make_point(static_cast<float>(r * -b), static_cast<float>(r * -0.5), 0.0f, 1.0f, 0.0f);

            geometry.make_polygon_reverse( {0, 1, 2} );

            geometry.make_point_corners();
            geometry.build_edges();
        }
    };
}

auto make_quad(const double edge) -> Geometry
{
    ERHE_PROFILE_FUNCTION

    //
    //  0.707106781 = sqrt(2) / 2
    // radius version:
    // make_point((float)(r * -0.707106781f), (float)(r * -0.707106781f), 0.0f, 0.0f, 0.0f);
    // make_point((float)(r *  0.707106781f), (float)(r * -0.707106781f), 0.0f, 1.0f, 0.0f);
    // make_point((float)(r *  0.707106781f), (float)(r *  0.707106781f), 0.0f, 1.0f, 1.0f);
    // make_point((float)(r * -0.707106781f), (float)(r *  0.707106781f), 0.0f, 0.0f, 1.0f);
    return Geometry{
        "quad",
        [=](auto& geometry)
        {
            geometry.make_point(static_cast<float>(edge * -0.5), static_cast<float>(edge * -0.5), 0.0f, 0.0f, 0.0f);
            geometry.make_point(static_cast<float>(edge *  0.5), static_cast<float>(edge * -0.5), 0.0f, 1.0f, 0.0f);
            geometry.make_point(static_cast<float>(edge *  0.5), static_cast<float>(edge *  0.5), 0.0f, 1.0f, 1.0f);
            geometry.make_point(static_cast<float>(edge * -0.5), static_cast<float>(edge *  0.5), 0.0f, 0.0f, 1.0f);

            geometry.make_polygon( {0, 1, 2, 3} );

            // Double sided
            geometry.make_polygon( {3, 2, 1, 0} );

            geometry.make_point_corners();
            geometry.build_edges();
        }
    };
}

auto make_rectangle(
    const double width,
    const double height,
    const bool   front_face,
    const bool   back_face
) -> Geometry
{
    ERHE_PROFILE_FUNCTION

    return Geometry{
        "rectangle",
        [=](auto& geometry)
        {
            geometry.make_point(static_cast<float>(width * -0.5), static_cast<float>(height * -0.5), 0.0f, 0.0f, 0.0f);
            geometry.make_point(static_cast<float>(width *  0.5), static_cast<float>(height * -0.5), 0.0f, 1.0f, 0.0f);
            geometry.make_point(static_cast<float>(width *  0.5), static_cast<float>(height *  0.5), 0.0f, 1.0f, 1.0f);
            geometry.make_point(static_cast<float>(width * -0.5), static_cast<float>(height *  0.5), 0.0f, 0.0f, 1.0f);

            // Texcoords X-flipped
            geometry.make_point(static_cast<float>(width * -0.5), static_cast<float>(height *  0.5), 0.0f, 1.0f, 1.0f);
            geometry.make_point(static_cast<float>(width *  0.5), static_cast<float>(height *  0.5), 0.0f, 0.0f, 1.0f);
            geometry.make_point(static_cast<float>(width *  0.5), static_cast<float>(height * -0.5), 0.0f, 0.0f, 0.0f);
            geometry.make_point(static_cast<float>(width * -0.5), static_cast<float>(height * -0.5), 0.0f, 1.0f, 0.0f);

            if (front_face)
            {
                geometry.make_polygon( {0, 1, 2, 3} );
            }

            if (back_face)
            {
                geometry.make_polygon( {4, 5, 6, 7} );
            }

            geometry.make_point_corners();
            geometry.build_edges();
        }
    };
}

} // namespace erhe::geometry::shapes
