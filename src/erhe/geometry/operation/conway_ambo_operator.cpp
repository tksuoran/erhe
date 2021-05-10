#include "erhe/geometry/operation/conway_ambo_operator.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace erhe::geometry::operation
{

Conway_ambo_operator::Conway_ambo_operator(Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ZoneScoped;

    make_polygon_centroids();
    make_edge_midpoints();

    {
        ZoneScopedN("Subdivide");

        // New faces from old points, new face corner for each old point corner edge midpoint
        for (Point_id src_point_id = 0,
             point_end = source.point_count();
             src_point_id < point_end;
             ++src_point_id)
        {
            Point&     src_point      = source.points[src_point_id];
            Polygon_id new_polygon_id = destination.make_polygon();

            for (Point_corner_id point_corner_id = src_point.first_point_corner_id,
                point_corner_end = src_point.first_point_corner_id + src_point.corner_count;
                point_corner_id < point_corner_end;
                ++point_corner_id)
            {
                Corner_id  src_corner_id      = source.point_corners[point_corner_id];
                Corner&    src_corner         = source.corners[src_corner_id];
                Polygon_id src_polygon_id     = src_corner.polygon_id;
                Polygon&   src_polygon        = source.polygons[src_polygon_id];
                Corner_id  src_next_corner_id = src_polygon.next_corner(source, src_corner_id);
                Corner&    src_next_corner    = source.corners[src_next_corner_id];
                Point_id   edge_midpoint      = get_edge_new_point(src_corner.point_id, src_next_corner.point_id);
                make_new_corner_from_point(new_polygon_id, edge_midpoint);
            }
        }

        // New faces from old faces, new face corner for each old corner edge midpoint
        for (Polygon_id src_polygon_id = 0,
             polygon_end = source.polygon_count();
             src_polygon_id < polygon_end;
             ++src_polygon_id)
        {
            Polygon&   src_polygon    = source.polygons[src_polygon_id];
            Polygon_id new_polygon_id = destination.make_polygon();
            for (uint32_t i = 0; i < src_polygon.corner_count; ++i)
            {
                Polygon_corner_id polygon_corner_id      = src_polygon.first_polygon_corner_id + i;
                Polygon_corner_id polygon_next_corner_id = src_polygon.first_polygon_corner_id + (i + 1) % src_polygon.corner_count;
                Corner_id         src_corner_id          = source.polygon_corners[polygon_corner_id];
                Corner_id         src_next_corner_id     = source.polygon_corners[polygon_next_corner_id];
                Corner&           src_corner             = source.corners[src_corner_id];
                Corner&           src_next_corner        = source.corners[src_next_corner_id];
                Point_id          edge_midpoint          = get_edge_new_point(src_corner.point_id, src_next_corner.point_id);
                make_new_corner_from_point(new_polygon_id, edge_midpoint);
            }
        }
    }

    post_processing();
}

auto ambo(Geometry& source) -> Geometry
{
    Geometry result(fmt::format("ambo({})", source.name));
    Conway_ambo_operator operation(source, result);
    return result;
}


} // namespace erhe::geometry::operation
