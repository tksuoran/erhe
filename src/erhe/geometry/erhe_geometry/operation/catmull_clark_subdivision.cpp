#include "erhe_geometry/operation/catmull_clark_subdivision.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::geometry::operation {

// E = average of two neighboring face points and original endpoints
//
// Compute P'             F = average F of all n face points for faces touching P
//                        R = average R of all n edge midpoints for edges touching P
//      F + 2R + (n-3)P   P = old point location
// P' = ----------------
//            n           -> F weight is     1/n
//                        -> R weight is     2/n
//      F   2R   (n-3)P   -> P weight is (n-3)/n
// P' = - + -- + ------
//      n    n      n
//
// For each corner in the old polygon, add one quad
// (centroid, previous edge 'edge midpoint', corner, next edge 'edge midpoint')
Catmull_clark_subdivision::Catmull_clark_subdivision(const Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ERHE_PROFILE_FUNCTION();

    //                       (n-3)P
    // Make initial P's with ------
    //                          n
    {
        ERHE_PROFILE_SCOPE("initial points");

        source.for_each_point_const([&](auto& i) {
            const auto n = static_cast<float>(i.point.corner_count);
            if (i.point.corner_count >= 3) {
                // n = 0   -> centroid points, safe to skip
                // n = 1,2 -> ?
                // n = 3   -> ?
                const float weight = (n - 3.0f) / n;
                make_new_point_from_point(weight, i.point_id);
            } else {
                make_new_point_from_point(1.0f, i.point_id);
            }
        });
    }

    // Make new edge midpoints
    // "average of two neighboring face points and original endpoints"
    // Add midpoint (R) to each new end points
    //   R = average R of all n edge midpoints for edges touching P
    //  2R  we add both edge end points with weight 1 so total edge weight is 2
    //  --
    //   n
    {
        ERHE_PROFILE_SCOPE("edge midpoints");

        reserve_edge_to_new_points();
        source.for_each_edge_const([&](auto& i) {
            const Point& src_point_a  = source.points[i.edge.a];
            const Point& src_point_b  = source.points[i.edge.b];
            const auto   new_point_id = find_or_make_point_from_edge(i.edge.a, i.edge.b);
            add_point_source(new_point_id, 1.0f, i.edge.a);
            add_point_source(new_point_id, 1.0f, i.edge.b);

            i.edge.for_each_polygon_const(source, [&](auto& j) {
                const auto weight = 1.0f / static_cast<float>(j.polygon.corner_count);
                add_polygon_centroid(new_point_id, weight, j.polygon_id);
            });
            const Point_id new_point_a_id = point_old_to_new.at(i.edge.a);
            const Point_id new_point_b_id = point_old_to_new.at(i.edge.b);

            const float n_a = static_cast<float>(src_point_a.corner_count);
            const float n_b = static_cast<float>(src_point_b.corner_count);
            ERHE_VERIFY(n_a != 0.0f);
            ERHE_VERIFY(n_b != 0.0f);
            const float weight_a = 1.0f / n_a;
            const float weight_b = 1.0f / n_b;
            add_point_source(new_point_a_id, weight_a, i.edge.a);
            add_point_source(new_point_a_id, weight_a, i.edge.b);
            add_point_source(new_point_b_id, weight_b, i.edge.a);
            add_point_source(new_point_b_id, weight_b, i.edge.b);
        });
    }

    {
        ERHE_PROFILE_SCOPE("face points");

        source.for_each_polygon_const([&](auto& i) {
            const Polygon& src_polygon = source.polygons[i.polygon_id];

            make_new_point_from_polygon_centroid(i.polygon_id);

            // Add polygon centroids (F) to all corners' point sources
            // F = average F of all n face points for faces touching P
            //  F    <- because F is average of all centroids, it adds extra /n
            // ---
            //  n
            i.polygon.for_each_corner_const(source, [&](auto& j)
            {
                const Point_id src_point_id = j.corner.point_id;
                const Point&   src_point    = source.points[src_point_id];
                const Point_id new_point_id = point_old_to_new[src_point_id];
                const auto point_weight  = 1.0f / static_cast<float>(src_point.corner_count);
                const auto corner_weight = 1.0f / static_cast<float>(src_polygon.corner_count);
                add_polygon_centroid(new_point_id, point_weight * point_weight * corner_weight, i.polygon_id);
            });
        });
    }

    // Subdivide polygons, clone (and corners);
    {
        ERHE_PROFILE_SCOPE("subdivide");

        source.for_each_polygon_const([&](auto& i) {
            i.polygon.for_each_corner_neighborhood_const(source, [&](auto& j) {
                const Point_id   previous_edge_midpoint = get_edge_new_point(j.prev_corner.point_id, j.corner.point_id);
                const Point_id   next_edge_midpoint     = get_edge_new_point(j.corner.point_id,      j.next_corner.point_id);
                const Polygon_id new_polygon_id         = make_new_polygon_from_polygon(i.polygon_id);
                make_new_corner_from_polygon_centroid(new_polygon_id, i.polygon_id);
                make_new_corner_from_point           (new_polygon_id, previous_edge_midpoint);
                make_new_corner_from_corner          (new_polygon_id, j.corner_id);
                make_new_corner_from_point           (new_polygon_id, next_edge_midpoint);
            });
        });
    }

    post_processing();

    log_catmull_clark->trace("Done");
}

auto catmull_clark_subdivision(const Geometry& source) -> Geometry
{
    return Geometry{
        fmt::format("catmull_clark({})", source.name),
        [&source](auto& result) {
            Catmull_clark_subdivision operation{source, result};
        }
    };
}

} // namespace erhe::geometry::operation
