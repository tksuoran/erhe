#include "erhe/geometry/operation/triangulate.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace erhe::geometry::operation
{

Triangulate::Triangulate(Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ZoneScoped;

    make_points_from_points();
    make_polygon_centroids();

    {
        ZoneScopedN("Subdivide");

        for (Polygon_id src_polygon_id = 0,
             polygon_end = source.polygon_count();
             src_polygon_id < polygon_end;
             ++src_polygon_id)
        {
            Polygon& src_polygon = source.polygons[src_polygon_id];

            if (src_polygon.corner_count == 3)
            {
                Polygon_id new_polygon_id = make_new_polygon_from_polygon(src_polygon_id);
                add_polygon_corners(new_polygon_id, src_polygon_id);
                continue;
            }

            for (uint32_t i = 0; i < src_polygon.corner_count; ++i)
            {
                Polygon_corner_id src_polygon_corner_id      = src_polygon.first_polygon_corner_id + i;
                Polygon_corner_id src_polygon_next_corner_id = src_polygon.first_polygon_corner_id + (i + 1) % src_polygon.corner_count;
                Corner_id         src_corner_id              = source.polygon_corners[src_polygon_corner_id];
                Corner_id         src_next_corner_id         = source.polygon_corners[src_polygon_next_corner_id];
                Polygon_id        new_polygon_id             = destination.make_polygon();
                make_new_corner_from_polygon_centroid(new_polygon_id, src_polygon_id);
                make_new_corner_from_corner(new_polygon_id, src_corner_id);
                make_new_corner_from_corner(new_polygon_id, src_next_corner_id);
            }
        }
    }

    post_processing();
}

auto triangulate(Geometry& source) -> Geometry
{
    Geometry result(fmt::format("triangulate({})", source.name));
    Triangulate operation(source, result);
    return result;
}


} // namespace erhe::geometry::operation
