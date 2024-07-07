#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/operation/truncate.hpp"
#include "erhe_geometry/types.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <fmt/core.h>
#include <vector>

namespace erhe::geometry::operation {

Truncate::Truncate(Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
    ERHE_PROFILE_FUNCTION();

    // Trisect each old edge by generating two new points.
    const float t0 = 1.0f / 3.0f;
    const float t1 = 2.0f / 3.0f;

    make_polygon_centroids();
    make_edge_midpoints( {t0, t1} );

    // New faces from old points, new face corner for each old point corner edge
    // 'midpoint' that is closest to the corner
    source.for_each_point_const([&](auto& i) {
        const Polygon_id new_polygon_id = destination.make_polygon();

        i.point.for_each_corner_const(source, [&](auto& j) {
            const Polygon&  src_polygon        = source.polygons[j.corner.polygon_id];
            const Corner_id src_next_corner_id = src_polygon.next_corner(source, j.corner_id);
            const Corner&   src_next_corner    = source.corners[src_next_corner_id];
            Point_id        edge_midpoint      = get_edge_new_point(j.corner.point_id, src_next_corner.point_id);
            if (src_next_corner.point_id > j.corner.point_id) {
                edge_midpoint += 1;
            }
            make_new_corner_from_point(new_polygon_id, edge_midpoint);
        });
    });

    // New faces from old faces, new face corner for each old corner edge 'midpoint'
    source.for_each_polygon([&](auto& i) {
        const Polygon_id new_polygon_id = destination.make_polygon();
        i.polygon.for_each_corner_neighborhood(source, [&](auto& j) {
            const Point_id edge_midpoint = get_edge_new_point(j.corner.point_id, j.next_corner.point_id);
            const Point_id point_a       = edge_midpoint;
            const Point_id point_b       = edge_midpoint + 1;
            if (j.next_corner.point_id > j.corner.point_id) {
                make_new_corner_from_point(new_polygon_id, point_b);
                make_new_corner_from_point(new_polygon_id, point_a);
            } else {
                make_new_corner_from_point(new_polygon_id, point_a);
                make_new_corner_from_point(new_polygon_id, point_b);
            }
        });
    });

    post_processing();
}

auto truncate(Geometry& source) -> Geometry
{
    return Geometry(
        fmt::format("truncate({})", source.name),
        [&source](auto& result) {
            Truncate operation{source, result};
        }
    );
}

} // namespace erhe::geometry::operation
