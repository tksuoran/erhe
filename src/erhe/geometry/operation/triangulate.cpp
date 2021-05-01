#include "erhe/geometry/operation/triangulate.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace erhe::geometry::operation
{

Triangulate::Triangulate(Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ZoneScoped;

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

    {
        ZoneScopedN("Polygon centroids");

        for (Polygon_id src_polygon_id = 0,
             polygon_end = m_source.polygon_count();
             src_polygon_id < polygon_end;
             ++src_polygon_id)
        {
            make_new_point_from_polygon_centroid(src_polygon_id);
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
                Polygon_corner_id src_polygon_corner_id      = src_polygon.first_polygon_corner_id + i;
                Polygon_corner_id src_polygon_next_corner_id = src_polygon.first_polygon_corner_id + (i + 1) % src_polygon.corner_count;
                Corner_id         src_corner_id              = m_source.polygon_corners[src_polygon_corner_id];
                Corner_id         src_next_corner_id         = m_source.polygon_corners[src_polygon_next_corner_id];
                Polygon_id        new_polygon_id             = m_destination.make_polygon();
                make_new_corner_from_polygon_centroid(new_polygon_id, src_polygon_id);
                make_new_corner_from_corner(new_polygon_id, src_corner_id);
                make_new_corner_from_corner(new_polygon_id, src_next_corner_id);
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

auto triangulate(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry
{
    erhe::geometry::Geometry result(fmt::format("triangulate({})", source.name()));
    Triangulate operation(source, result);
    return result;
}


} // namespace erhe::geometry::operation