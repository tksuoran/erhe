#include "erhe/geometry/operation/subdivide.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/geometry_log.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>

namespace erhe::geometry::operation
{

Subdivide::Subdivide(Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ERHE_PROFILE_FUNCTION();

    // Add midpoints to edges and connect to polygon center

    // For each corner in the old polygon,
    // add one quad (centroid, previous edge midpoint, corner, next edge midpoint)

    make_points_from_points();
    make_polygon_centroids();
    make_edge_midpoints();

    source.for_each_polygon_const([&](auto& i) {
        //if (src_polygon.corner_count == 3)
        //{
        //    Polygon_id new_polygon_id = make_new_polygon_from_polygon(src_polygon_id);
        //    add_polygon_corners(new_polygon_id, src_polygon_id);
        //    continue;
        //}
        i.polygon.for_each_corner_neighborhood_const(source, [&](auto& j) {
            const Point_id   a                      = j.prev_corner.point_id;
            const Point_id   b                      = j.corner     .point_id;
            const Point_id   c                      = j.next_corner.point_id;
            const Polygon_id new_polygon_id         = make_new_polygon_from_polygon(i.polygon_id);
            const Point_id   previous_edge_midpoint = get_edge_new_point(a, b);
            const Point_id   next_edge_midpoint     = get_edge_new_point(b, c);
            if (previous_edge_midpoint == std::numeric_limits<uint32_t>::max()) {
                log_subdivide->warn("midpoint for edge {} {} not found", std::min(a, b), std::max(a, b));
                return;
            }
            if (next_edge_midpoint == std::numeric_limits<uint32_t>::max()) {
                log_subdivide->warn("midpoint for edge {} {} not found", std::min(b, c), std::max(b, c));
                return;
            }
            make_new_corner_from_point           (new_polygon_id, previous_edge_midpoint);
            make_new_corner_from_corner          (new_polygon_id, j.corner_id);
            make_new_corner_from_point           (new_polygon_id, next_edge_midpoint);
            make_new_corner_from_polygon_centroid(new_polygon_id, i.polygon_id);
            //log_subdivide.warn(
            //    "Polygon {} = {} {} {} {}\n",
            //    new_polygon_id,
            //    previous_edge_midpoint,
            //    source.corners[j.corner_id].point_id,
            //    next_edge_midpoint,
            //    old_polygon_centroid_to_new_points[i.polygon_id]
            //);
        });
    });

    post_processing();
}

auto subdivide(Geometry& source) -> Geometry
{
    return Geometry{
        fmt::format("subdivide({})", source.name),
        [&source](auto& result) {
            Subdivide operation{source, result};
        }
    };
}


} // namespace erhe::geometry::operation
