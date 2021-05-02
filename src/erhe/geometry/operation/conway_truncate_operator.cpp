#include "erhe/geometry/operation/conway_truncate_operator.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace erhe::geometry::operation
{

Conway_truncate_operator::Conway_truncate_operator(Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ZoneScoped;

    // Generate two 'midpoints' fo each edge.
    float t0 = 1.0f / 3.0f;
    float t1 = 2.0f / 3.0f;

    make_polygon_centroids();
    make_edge_midpoints( {t0, t1} );

    {
        ZoneScopedN("Subdivide");

        // New faces from old points, new face corner for each old point corner edge
        // 'midpoint' that is closest to the corner
        for (Point_id src_point_id = 0,
             point_end = m_source.point_count();
             src_point_id < point_end;
             ++src_point_id)
        {
            Point&     src_point      = m_source.points[src_point_id];
            Polygon_id new_polygon_id = m_destination.make_polygon();

            for (Point_corner_id point_corner_id = src_point.first_point_corner_id,
                point_corner_end = src_point.first_point_corner_id + src_point.corner_count;
                point_corner_id < point_corner_end;
                ++point_corner_id)
            {
                Corner_id  src_corner_id      = m_source.point_corners[point_corner_id];
                Corner&    src_corner         = m_source.corners[src_corner_id];
                Polygon_id src_polygon_id     = src_corner.polygon_id;
                Polygon&   src_polygon        = m_source.polygons[src_polygon_id];
                Corner_id  src_next_corner_id = src_polygon.next_corner(m_source, src_corner_id);
                Corner&    src_next_corner    = m_source.corners[src_next_corner_id];
                Point_id   edge_midpoint      = get_edge_midpoint(src_corner.point_id, src_next_corner.point_id);
                if (src_next_corner.point_id > src_corner.point_id)
                {
                    edge_midpoint += 1;
                }
                make_new_corner_from_point(new_polygon_id, edge_midpoint);
            }
        }

        // New faces from old faces, new face corner for each old corner edge 'midpoint'
        for (Polygon_id src_polygon_id = 0,
             polygon_end = m_source.polygon_count();
             src_polygon_id < polygon_end;
             ++src_polygon_id)
        {
            Polygon&   src_polygon    = m_source.polygons[src_polygon_id];
            Polygon_id new_polygon_id = m_destination.make_polygon();
            for (uint32_t i = 0; i < src_polygon.corner_count; ++i)
            {
                Polygon_corner_id polygon_corner_id      = src_polygon.first_polygon_corner_id + i;
                Polygon_corner_id polygon_next_corner_id = src_polygon.first_polygon_corner_id + (i + 1) % src_polygon.corner_count;
                Corner_id         src_corner_id          = m_source.polygon_corners[polygon_corner_id];
                Corner_id         src_next_corner_id     = m_source.polygon_corners[polygon_next_corner_id];
                Corner&           src_corner             = m_source.corners[src_corner_id];
                Corner&           src_next_corner        = m_source.corners[src_next_corner_id];
                Point_id          edge_midpoint          = get_edge_midpoint(src_corner.point_id, src_next_corner.point_id);
                Point_id          point_a = edge_midpoint;
                Point_id          point_b = edge_midpoint + 1;
                if (src_next_corner.point_id > src_corner.point_id)
                {
                    std::swap(point_a, point_b);
                }
                make_new_corner_from_point(new_polygon_id, point_a);
                make_new_corner_from_point(new_polygon_id, point_b);
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

auto truncate(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry
{
    erhe::geometry::Geometry result(fmt::format("truncate({})", source.name()));
    Conway_truncate_operator operation(source, result);
    return result;
}


} // namespace erhe::geometry::operation
