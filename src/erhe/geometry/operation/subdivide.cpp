#include "erhe/geometry/operation/subdivide.hpp"
#include "erhe/geometry/log.hpp"
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
    {
        ZoneScopedN("Points");

        for (Point_id src_point_id = 0,
             point_end = m_source.point_count();
             src_point_id < point_end;
             ++src_point_id)
        {
            make_new_point_from_point(src_point_id);
        }
    }

    std::vector<Point_id> old_edge_to_new_midpoints;
    uint32_t point_count = m_source.point_count();
    old_edge_to_new_midpoints.resize(point_count * point_count);
    std::fill(begin(old_edge_to_new_midpoints), end(old_edge_to_new_midpoints), std::numeric_limits<uint32_t>::max());

    {
        ZoneScopedN("Polygon centroids");

        for (Polygon_id src_polygon_id = 0,
             polygon_end = m_source.polygon_count();
             src_polygon_id < polygon_end;
             ++src_polygon_id)
        {
            Polygon& src_polygon = m_source.polygons[src_polygon_id];
            make_new_point_from_polygon_centroid(src_polygon_id);
            //for(int i = 0; i < oldPolygon.Corners.Count; ++i)
            //{
            //    Corner  corner1 = oldPolygon.Corners[i];
            //    Corner  corner2 = oldPolygon.Corners[(i + 1) % oldPolygon.Corners.Count];
            //    UpdateNewEdgeFromCorners(corner1, corner2);
            //}
            for (uint32_t i = 0; i < src_polygon.corner_count; ++i)
            {
                Polygon_corner_id src_polygon_corner_id      = src_polygon.first_polygon_corner_id + i;
                Polygon_corner_id src_polygon_next_corner_id = src_polygon.first_polygon_corner_id + (i + 1) % src_polygon.corner_count;
                Corner_id         src_corner_id              = m_source.polygon_corners[src_polygon_corner_id];
                Corner_id         src_next_corner_id         = m_source.polygon_corners[src_polygon_next_corner_id];
                Corner&           src_corner                 = m_source.corners[src_corner_id];
                Corner&           src_next_corner            = m_source.corners[src_next_corner_id];
                Point_id          a                          = src_corner.point_id;
                Point_id          b                          = src_next_corner.point_id;
                uint32_t          edge_key = std::min(a, b) * point_count + std::max(a, b);

                Point_id new_midpoint;
                if (old_edge_to_new_midpoints[edge_key] == std::numeric_limits<uint32_t>::max())
                {
                    new_midpoint = destination.make_point();
                    old_edge_to_new_midpoints[edge_key] = new_midpoint;
                    log_subdivide.trace("created midpoint {} on edge {} {}\n", new_midpoint, std::min(a, b), std::max(a, b));
                }
                else
                {
                    new_midpoint = old_edge_to_new_midpoints[edge_key];
                    log_subdivide.trace("using midpoint {} on edge {} {}\n", new_midpoint, std::min(a, b), std::max(a, b));
                }
                add_point_source(new_midpoint, 0.5f, a);
                add_point_source(new_midpoint, 0.5f, b);
                add_point_corner_source(new_midpoint, 0.5f, src_corner_id);
                add_point_corner_source(new_midpoint, 0.5f, src_next_corner_id);
            }
        }
    }

    {
        ZoneScopedN("Subdivide");

        for (Polygon_id src_polygon_id = 0,
             polygon_end = m_source.polygon_count();
             src_polygon_id < polygon_end;
             ++src_polygon_id)
        {
            Polygon& src_polygon = m_source.polygons[src_polygon_id];

            if (src_polygon.corner_count == 3)
            {
                Polygon_id new_polygon_id = make_new_polygon_from_polygon(src_polygon_id);
                add_polygon_corners(new_polygon_id, src_polygon_id);
                continue;
            }

            for (uint32_t i = 0; i < src_polygon.corner_count; ++i)
            {
                Polygon_corner_id src_polygon_next_corner_id     = src_polygon.first_polygon_corner_id + (src_polygon.corner_count + i - 1) % src_polygon.corner_count;
                Polygon_corner_id src_polygon_corner_id          = src_polygon.first_polygon_corner_id + i;
                Polygon_corner_id src_polygon_previous_corner_id = src_polygon.first_polygon_corner_id + (i + 1) % src_polygon.corner_count;
                Corner_id         src_previous_corner_id         = m_source.polygon_corners[src_polygon_previous_corner_id];
                Corner_id         src_corner_id                  = m_source.polygon_corners[src_polygon_corner_id];
                Corner_id         src_next_corner_id             = m_source.polygon_corners[src_polygon_next_corner_id];
                Corner&           src_previous_corner            = m_source.corners[src_previous_corner_id];
                Corner&           src_corner                     = m_source.corners[src_corner_id];
                Corner&           src_next_corner                = m_source.corners[src_next_corner_id];
                Point_id          a                              = src_previous_corner.point_id;
                Point_id          b                              = src_corner.point_id;
                Point_id          c                              = src_next_corner.point_id;
                uint32_t          previous_edge_key              = std::min(a, b) * point_count + std::max(a, b);
                uint32_t          next_edge_key                  = std::min(b, c) * point_count + std::max(b, c);
                Polygon_id        new_polygon_id                 = make_new_polygon_from_polygon(src_polygon_id);
                Point_id          previous_edge_midpoint         = old_edge_to_new_midpoints[previous_edge_key];
                Point_id          next_edge_midpoint             = old_edge_to_new_midpoints[next_edge_key];
                if (previous_edge_midpoint == std::numeric_limits<uint32_t>::max())
                {
                    log_subdivide.warn("midpoint for edge {} {} not found\n", std::min(a, b), std::max(a, b));
                    continue;
                }
                if (next_edge_midpoint == std::numeric_limits<uint32_t>::max())
                {
                    log_subdivide.warn("midpoint for edge {} {} not found\n", std::min(b, c), std::max(b, c));
                    continue;
                }
                make_new_corner_from_polygon_centroid(new_polygon_id, src_polygon_id);
                make_new_corner_from_point(new_polygon_id, next_edge_midpoint);
                make_new_corner_from_corner(new_polygon_id, src_corner_id);
                make_new_corner_from_point(new_polygon_id, previous_edge_midpoint);
            }
        }
    }

    {
        ZoneScopedN("post-processing");

        destination.make_point_corners();
        destination.build_edges();
        interpolate_all_property_maps();
        destination.compute_point_normals(c_point_normals_smooth);
        destination.compute_polygon_centroids();
        destination.generate_polygon_texture_coordinates();
        destination.compute_tangents();
    }
}

auto subdivide(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry
{
    erhe::geometry::Geometry result(fmt::format("subdivide({})", source.name()));
    Subdivide operation(source, result);
    return result;
}


} // namespace erhe::geometry::operation