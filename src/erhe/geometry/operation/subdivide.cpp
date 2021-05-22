#include "erhe/geometry/operation/subdivide.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/log.hpp"

#include "Tracy.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cstdint>
#include <limits>

namespace erhe::geometry::operation
{

Subdivide::Subdivide(Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ZoneScoped;

    // Add midpoints to edges and connect to polygon center

    // For each corner in the old polygon,
    // add one quad (centroid, previous edge midpoint, corner, next edge midpoint)

    make_points_from_points();
    make_polygon_centroids();
    make_edge_midpoints();

    source.for_each_polygon([&](auto& i)
    {
        //if (src_polygon.corner_count == 3)
        //{
        //    Polygon_id new_polygon_id = make_new_polygon_from_polygon(src_polygon_id);
        //    add_polygon_corners(new_polygon_id, src_polygon_id);
        //    continue;
        //}
        i.polygon.for_each_corner_neighborhood(source, [&](auto& j)
        {
            Point_id   a                      = j.prev_corner.point_id;
            Point_id   b                      = j.corner     .point_id;
            Point_id   c                      = j.next_corner.point_id;
            Polygon_id new_polygon_id         = make_new_polygon_from_polygon(i.polygon_id);
            Point_id   previous_edge_midpoint = get_edge_new_point(a, b);
            Point_id   next_edge_midpoint     = get_edge_new_point(b, c);
            if (previous_edge_midpoint == std::numeric_limits<uint32_t>::max())
            {
                log_subdivide.warn("midpoint for edge {} {} not found\n", std::min(a, b), std::max(a, b));
                return;
            }
            if (next_edge_midpoint == std::numeric_limits<uint32_t>::max())
            {
                log_subdivide.warn("midpoint for edge {} {} not found\n", std::min(b, c), std::max(b, c));
                return;
            }
            make_new_corner_from_polygon_centroid(new_polygon_id, i.polygon_id);
            make_new_corner_from_point           (new_polygon_id, next_edge_midpoint);
            make_new_corner_from_corner          (new_polygon_id, j.corner_id);
            make_new_corner_from_point           (new_polygon_id, previous_edge_midpoint);
        });
    });

    post_processing();
}

auto subdivide(Geometry& source) -> Geometry
{
    Geometry result(fmt::format("subdivide({})", source.name));
    Subdivide operation(source, result);
    return result;
}


} // namespace erhe::geometry::operation
