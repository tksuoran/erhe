#include "erhe/geometry/operation/ambo.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/toolkit/profile.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace erhe::geometry::operation
{

Ambo::Ambo(Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
    ERHE_PROFILE_FUNCTION();

    make_polygon_centroids();
    make_edge_midpoints();

    // New faces from old points, new face corner for each old point corner edge midpoint
    {
        ERHE_PROFILE_SCOPE("new faces from old points");
        source.for_each_point_const([&](auto& i)
        {
            Polygon_id new_polygon_id = destination.make_polygon();

            i.point.for_each_corner_const(source, [&](auto& j)
            {
                const Polygon_id src_polygon_id     = j.corner.polygon_id;
                const Polygon&   src_polygon        = source.polygons[src_polygon_id];
                const Corner_id  src_next_corner_id = src_polygon.next_corner(source, j.corner_id);
                const Corner&    src_next_corner    = source.corners[src_next_corner_id];
                const Point_id   edge_midpoint      = get_edge_new_point(j.corner.point_id, src_next_corner.point_id);
                make_new_corner_from_point(new_polygon_id, edge_midpoint);
            });
        });
    }

    // New faces from old faces, new face corner for each old corner edge midpoint
    {
        ERHE_PROFILE_SCOPE("new faces from old faces");
        source.for_each_polygon_const([&](auto& i)
        {
            const Polygon_id new_polygon_id = destination.make_polygon();
            i.polygon.for_each_corner_neighborhood_const(source, [&](auto& j)
            {
                const Point_id edge_midpoint = get_edge_new_point(j.corner.point_id, j.next_corner.point_id);
                make_new_corner_from_point(new_polygon_id, edge_midpoint);
            });
        });
    }

    post_processing();
}

auto ambo(Geometry& source) -> Geometry
{
    return Geometry{
        fmt::format("ambo({})", source.name),
        [&source](auto& result)
        {
            Ambo operation{source, result};
        }
    };
}


} // namespace erhe::geometry::operation
