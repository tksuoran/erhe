#include "erhe/geometry/operation/gyro.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/geometry_log.hpp"
#include "erhe/toolkit/profile.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>

namespace erhe::geometry::operation
{

Gyro::Gyro(Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ERHE_PROFILE_FUNCTION();

    // Add midpoints 1/3 and 2/3 to edges and connect to polygon center
    // New polygon on each old edge

    // For each corner in the old polygon,
    // add one pentagon(centroid, previous edge midpoints 0 and 1, corner, next edge midpoint 0)

    make_points_from_points();
    make_polygon_centroids();
    make_edge_midpoints(
        {
            1.0f / 3.0f,
            2.0f / 3.0f
        }
    );

    source.for_each_polygon_const([&](auto& i) {
        i.polygon.for_each_corner_neighborhood_const(source, [&](auto& j) {
            const Point_id   a                        = j.prev_corner.point_id;
            const Point_id   b                        = j.corner     .point_id;
            const Point_id   c                        = j.next_corner.point_id;
            const Polygon_id new_polygon_id           = make_new_polygon_from_polygon(i.polygon_id);
            const Point_id   previous_edge_midpoint_0 = get_edge_new_point(a, b, 0, 2);
            const Point_id   previous_edge_midpoint_1 = get_edge_new_point(a, b, 1, 2);
            const Point_id   next_edge_midpoint_0     = get_edge_new_point(b, c, 0, 2);
            if (previous_edge_midpoint_0 == std::numeric_limits<uint32_t>::max()) {
                log_subdivide->warn("midpoint for edge {} {} [0] not found", a, b);
                return;
            }
            if (previous_edge_midpoint_1 == std::numeric_limits<uint32_t>::max()) {
                log_subdivide->warn("midpoint for edge {} {} [1] not found", a, b);
                return;
            }
            if (next_edge_midpoint_0 == std::numeric_limits<uint32_t>::max()) {
                log_subdivide->warn("midpoint for edge {} {} [0] not found", b, c);
                return;
            }
            make_new_corner_from_point           (new_polygon_id, previous_edge_midpoint_0);
            make_new_corner_from_point           (new_polygon_id, previous_edge_midpoint_1);
            make_new_corner_from_corner          (new_polygon_id, j.corner_id);
            make_new_corner_from_point           (new_polygon_id, next_edge_midpoint_0);
            make_new_corner_from_polygon_centroid(new_polygon_id, i.polygon_id);
            //log_subdivide.warn(
            //    "Polygon {} = {} {} {} {} {}\n",
            //    new_polygon_id,
            //    previous_edge_midpoint_0,
            //    previous_edge_midpoint_1,
            //    source.corners[j.corner_id].point_id,
            //    next_edge_midpoint_0,
            //    old_polygon_centroid_to_new_points[i.polygon_id]
            //);
        });
    });

    post_processing();
}

auto gyro(Geometry& source) -> Geometry
{
    return Geometry{
        fmt::format("gyro({})", source.name),
        [&source](auto& result) {
            Gyro operation{source, result};
        }
    };
}


} // namespace erhe::geometry::operation
