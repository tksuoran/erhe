#include "erhe/geometry/operation/conway_dual_operator.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace erhe::geometry::operation
{

Conway_dual_operator::Conway_dual_operator(Geometry& src, Geometry& destination, bool post_process)
    : Geometry_operation{src, destination}
{
    ZoneScoped;

    make_polygon_centroids();

    {
        ZoneScopedN("Subdivide");

        // New faces from old points, new face corner for each old point corner
        for (Point_id src_point_id = 0,
             point_end = source.point_count();
             src_point_id < point_end;
             ++src_point_id)
        {
            Point&     src_point = source.points[src_point_id];
            Polygon_id new_polygon_id = destination.make_polygon();

            for (Point_corner_id point_corner_id = src_point.first_point_corner_id,
                point_corner_end = src_point.first_point_corner_id + src_point.corner_count;
                point_corner_id < point_corner_end;
                ++point_corner_id)
            {
                Corner_id  src_corner_id  = source.point_corners[point_corner_id];
                Corner&    src_corner     = source.corners[src_corner_id];
                Polygon_id src_polygon_id = src_corner.polygon_id;
                make_new_corner_from_polygon_centroid(new_polygon_id, src_polygon_id);
            }
        }
    }

    if (post_process)
    {
        post_processing();
    }
}

auto dual(Geometry& source) -> Geometry
{
    Geometry result(fmt::format("dual({})", source.name));
    Conway_dual_operator operation(source, result);
    return result;
}


} // namespace erhe::geometry::operation
