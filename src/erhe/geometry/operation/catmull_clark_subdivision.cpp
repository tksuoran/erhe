#include "erhe/geometry/operation/catmull_clark_subdivision.hpp"
#include "erhe/geometry/log.hpp"
#include "Tracy.hpp"
#include <gsl/assert>

namespace erhe::geometry::operation
{

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
Catmull_clark_subdivision::Catmull_clark_subdivision(Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ZoneScoped;

    //                       (n-3)P
    // Make initial P's with ------
    //                          n
    {
        ZoneScopedN("initial points");
        log_catmull_clark.trace("Make initial points = {} source points\n", source.point_count());
        erhe::log::Log::Indenter scope_indent;

        for (Point_id src_point_id = 0,
             point_end = source.point_count();
             src_point_id < point_end;
             ++src_point_id)
        {
            Point& src_point = source.points[src_point_id];
            auto n = static_cast<float>(src_point.corner_count);
            if (n == 0.0f)
            {
                // TODO debug and fix
                continue;
            }
            Expects(n != 0.0f);

            float weight = (n - 3.0f) / n;
            make_new_point_from_point(weight, src_point_id);
        }
    }

    // Make new edge midpoints
    // "average of two neighboring face points and original endpoints"
    // Add midpoint (R) to each new end points
    //   R = average R of all n edge midpoints for edges touching P
    //  2R  we add both edge end points with weight 1 so total edge weight is 2
    //  --
    //   n
    {
        ZoneScopedN("edge midpoints");

        log_catmull_clark.trace("\nMake new edge midpoints - {} source edges\n", source.edge_count());
        erhe::log::Log::Indenter scope_indent;

        reserve_edge_to_new_points();
        for (Edge_id src_edge_id = 0,
             edge_end = source.edge_count();
             src_edge_id < edge_end;
             ++src_edge_id)
        {
            Edge&  src_edge     = source.edges[src_edge_id];
            Point& src_point_a  = source.points[src_edge.a];
            Point& src_point_b  = source.points[src_edge.b];
            auto   new_point_id = find_or_make_point_from_edge(src_edge.a, src_edge.b);
            add_point_source(new_point_id, 1.0f, src_edge.a);
            add_point_source(new_point_id, 1.0f, src_edge.b);

            for (Edge_polygon_id edge_polygon_id = src_edge.first_edge_polygon_id,
                 end = src_edge.first_edge_polygon_id + src_edge.polygon_count;
                 edge_polygon_id < end;
                 ++edge_polygon_id)
            {
                Polygon_id src_polygon_id = source.edge_polygons[edge_polygon_id];
                Polygon&   src_polygon    = source.polygons[src_polygon_id];

                auto weight = 1.0f / static_cast<float>(src_polygon.corner_count);
                add_polygon_centroid(new_point_id, weight, src_polygon_id);
            }
            Point_id new_point_a_id = point_old_to_new[src_edge.a];
            Point_id new_point_b_id = point_old_to_new[src_edge.b];

            auto n_a = static_cast<float>(src_point_a.corner_count);
            auto n_b = static_cast<float>(src_point_b.corner_count);
            Expects(n_a != 0.0f);
            Expects(n_b != 0.0f);
            float weight_a = 1.0f / n_a;
            float weight_b = 1.0f / n_b;
            add_point_source(new_point_a_id, weight_a, src_edge.a);
            add_point_source(new_point_a_id, weight_a, src_edge.b);
            add_point_source(new_point_b_id, weight_b, src_edge.a);
            add_point_source(new_point_b_id, weight_b, src_edge.b);
        }
    }

    {
        log_catmull_clark.trace("\nMake new points from centroid - {} source polygons\n", source.polygon_count());
        erhe::log::Log::Indenter scope_indent;
        ZoneScopedN("face points");

        for (Polygon_id src_polygon_id = 0,
             polygon_end = source.polygon_count();
             src_polygon_id < polygon_end;
             ++src_polygon_id)
        {
            log_catmull_clark.trace("old polygon id = {}\n", src_polygon_id);
            erhe::log::Log::Indenter scope_indent;

            Polygon& src_polygon = source.polygons[src_polygon_id];

            // Make centroid point
            make_new_point_from_polygon_centroid(src_polygon_id);

            // Add polygon centroids (F) to all corners' point sources
            // F = average F of all n face points for faces touching P
            //  F    <- because F is average of all centroids, it adds extra /n
            // ---
            //  n
            for (Polygon_corner_id polygon_corner_id = src_polygon.first_polygon_corner_id,
                 corner_end = src_polygon.first_polygon_corner_id + src_polygon.corner_count;
                 polygon_corner_id < corner_end;
                 ++polygon_corner_id)
            {
                Corner_id src_corner_id = source.polygon_corners[polygon_corner_id];
                Corner&   src_corner    = source.corners[src_corner_id];
                Point_id  src_point_id  = src_corner.point_id;
                Point&    src_point     = source.points[src_point_id];
                Point_id  new_point_id  = point_old_to_new[src_point_id];

                log_catmull_clark.trace("old corner_id {} old point id {} new point id {}\n",
                                        src_corner_id, src_point_id, new_point_id);
                erhe::log::Log::Indenter scope_indent;

                auto point_weight  = 1.0f / static_cast<float>(src_point.corner_count);
                auto corner_weight = 1.0f / static_cast<float>(src_polygon.corner_count);
                add_polygon_centroid(new_point_id, point_weight * point_weight * corner_weight, src_polygon_id);
            }
        }
    }

    // Subdivide polygons, clone (and corners);
    {
        ZoneScopedN("subdivide");

        log_catmull_clark.trace("\nSubdivide polygons\n");
        erhe::log::Log::Indenter scope_indent;

        for (Polygon_id src_polygon_id = 0,
             polygon_end = source.polygon_count();
             src_polygon_id < polygon_end;
             ++src_polygon_id)
        {
            Polygon& src_polygon = source.polygons[src_polygon_id];

            log_catmull_clark.trace("Source polygon {}:\n", src_polygon_id);
            erhe::log::Log::Indenter scope_indent;

            for (uint32_t i = 0; i < src_polygon.corner_count; ++i)
            {
                Polygon_corner_id polygon_corner_id          = src_polygon.first_polygon_corner_id + i;
                Polygon_corner_id previous_polygon_corner_id = src_polygon.first_polygon_corner_id + ((src_polygon.corner_count + i - 1) % src_polygon.corner_count);
                Polygon_corner_id next_polygon_corner_id     = src_polygon.first_polygon_corner_id + ((src_polygon.corner_count + i + 1) % src_polygon.corner_count);
                Corner_id         src_corner_id              = source.polygon_corners[polygon_corner_id];
                Corner_id         previous_corner_id         = source.polygon_corners[previous_polygon_corner_id];
                Corner_id         next_corner_id             = source.polygon_corners[next_polygon_corner_id];
                Corner&           src_corner                 = source.corners[src_corner_id];
                Corner&           previous_corner            = source.corners[previous_corner_id];
                Corner&           next_corner                = source.corners[next_corner_id];
                Point_id          previous_edge_midpoint     = get_edge_new_point(previous_corner.point_id, src_corner.point_id);
                Point_id          next_edge_midpoint         = get_edge_new_point(src_corner.point_id,      next_corner.point_id);
                Polygon_id        new_polygon_id             = make_new_polygon_from_polygon(src_polygon_id);
                make_new_corner_from_polygon_centroid(new_polygon_id, src_polygon_id);
                make_new_corner_from_point(new_polygon_id, previous_edge_midpoint);
                make_new_corner_from_corner(new_polygon_id, src_corner_id);
                make_new_corner_from_point(new_polygon_id, next_edge_midpoint);
            }
        }
    }

    post_processing();

    log_catmull_clark.trace("Done\n");
}

auto catmull_clark_subdivision(Geometry& source) -> Geometry
{
    Geometry result(fmt::format("catmull_clark({})", source.name));
    Catmull_clark_subdivision operation(source, result);
    return result;
}

} // namespace erhe::geometry::operation
