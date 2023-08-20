#include "erhe_geometry/operation/meta.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_profile/profile.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>

namespace erhe::geometry::operation
{

Meta::Meta(Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ERHE_PROFILE_FUNCTION();

    // Add midpoints to edges and connect to polygon center

    // For each corner in the old polygon,
    // add two triangles (centroid, previous edge midpoint, corner), (centroid, corner, next edge midpoint)

    make_points_from_points();
    make_polygon_centroids();
    make_edge_midpoints();

    source.for_each_polygon_const([&](auto& i) {
        i.polygon.for_each_corner_neighborhood_const(source, [&](auto& j) {
            const Point_id   a                      = j.prev_corner.point_id;
            const Point_id   b                      = j.corner     .point_id;
            const Point_id   c                      = j.next_corner.point_id;
            const Polygon_id new_polygon_id_a       = make_new_polygon_from_polygon(i.polygon_id);
            const Polygon_id new_polygon_id_b       = make_new_polygon_from_polygon(i.polygon_id);
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
            make_new_corner_from_polygon_centroid(new_polygon_id_a, i.polygon_id);
            make_new_corner_from_point           (new_polygon_id_a, previous_edge_midpoint);
            make_new_corner_from_corner          (new_polygon_id_a, j.corner_id);

            make_new_corner_from_polygon_centroid(new_polygon_id_b, i.polygon_id);
            make_new_corner_from_corner          (new_polygon_id_b, j.corner_id);
            make_new_corner_from_point           (new_polygon_id_b, next_edge_midpoint);
        });
    });

    post_processing();
}

auto meta(Geometry& source) -> Geometry
{
    return Geometry{
        fmt::format("meta({})", source.name),
        [&source](auto& result) {
            Meta operation{source, result};
        }
    };
}


} // namespace erhe::geometry::operation
