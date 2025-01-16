#include "erhe_geometry/operation/join.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <sstream>

namespace erhe::geometry::operation {

Join::Join(const Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ERHE_PROFILE_FUNCTION();

    make_points_from_points();
    make_polygon_centroids();

    source.for_each_edge_const([&](const auto& i) {
        if (i.edge.polygon_count != 2) {
            return;
        }
        const Polygon_id new_polygon_id = destination.make_polygon();
        const Point_id   point_id_a     = i.edge.a;
        const Point_id   point_id_b     = i.edge.b;
        //const Point&     point_a        = src.points[point_id_a];
        //const Point&     point_b        = src.points[point_id_b];
        const Polygon_id polygon_id_l   = src.edge_polygons.at(i.edge.first_edge_polygon_id    );
        const Polygon_id polygon_id_r   = src.edge_polygons.at(i.edge.first_edge_polygon_id + 1);
        const Polygon&   polygon_l      = src.polygons.at(polygon_id_l);
        const Polygon&   polygon_r      = src.polygons.at(polygon_id_r);
        bool l_forward {false}; // a, b
        bool l_backward{false}; // b, a
        std::stringstream l_ss;
        polygon_l.for_each_corner_neighborhood_const(src, [&](auto& j) {
            l_ss << j.corner.point_id << " ";
            if (j.corner.point_id == point_id_a) {
                if (j.prev_corner.point_id == point_id_b) {
                    l_backward = true;
                    //j.break_ = true;
                    //return;
                    ERHE_VERIFY(l_forward != l_backward);
                }
                if (j.next_corner.point_id == point_id_b) {
                    l_forward = true;
                    //j.break_ = true;
                    //return;
                    ERHE_VERIFY(l_forward != l_backward);
                }
            }
        });
        bool r_forward {false};
        bool r_backward{false};
        std::stringstream r_ss;
        polygon_r.for_each_corner_neighborhood_const(src, [&](auto& j) {
            r_ss << j.corner.point_id << " ";
            if (j.corner.point_id == point_id_a) {
                if (j.prev_corner.point_id == point_id_b) {
                    r_backward = true;
                    //j.break_ = true;
                    //return;
                    ERHE_VERIFY(r_forward != r_backward);
                }
                if (j.next_corner.point_id == point_id_b) {
                    r_forward = true;
                    //j.break_ = true;
                    //return;
                    ERHE_VERIFY(r_forward != r_backward);
                }
            }
        });
        ERHE_VERIFY(l_forward != l_backward);
        ERHE_VERIFY(r_forward != r_backward);
        ERHE_VERIFY(l_forward != r_forward);

        if (l_forward) {
            make_new_corner_from_point           (new_polygon_id, point_id_a  );
            make_new_corner_from_polygon_centroid(new_polygon_id, polygon_id_r);
            make_new_corner_from_point           (new_polygon_id, point_id_b  );
            make_new_corner_from_polygon_centroid(new_polygon_id, polygon_id_l);
        } else {
            make_new_corner_from_point           (new_polygon_id, point_id_a  );
            make_new_corner_from_polygon_centroid(new_polygon_id, polygon_id_l);
            make_new_corner_from_point           (new_polygon_id, point_id_b  );
            make_new_corner_from_polygon_centroid(new_polygon_id, polygon_id_r);
        }
    });

    post_processing();
}

auto join(const Geometry& source) -> Geometry
{
    return Geometry{
        fmt::format("join({})", source.name),
        [&source](auto& result) {
            Join operation{source, result};
        }
    };
}


} // namespace erhe::geometry::operation
