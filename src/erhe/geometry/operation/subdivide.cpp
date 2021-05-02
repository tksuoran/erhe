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

    make_points_from_points();
    make_polygon_centroids();
    make_edge_midpoints();

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
                Polygon_id        new_polygon_id                 = make_new_polygon_from_polygon(src_polygon_id);
                Point_id          previous_edge_midpoint         = get_edge_midpoint(a, b);
                Point_id          next_edge_midpoint             = get_edge_midpoint(b, c);
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
