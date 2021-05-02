#include "erhe/geometry/operation/conway_dual_operator.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace erhe::geometry::operation
{

Conway_dual_operator::Conway_dual_operator(Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ZoneScoped;

    make_polygon_centroids();

    {
        ZoneScopedN("Subdivide");

        // New faces from old points, new face corner for each old point corner
        for (Point_id src_point_id = 0,
             point_end = m_source.point_count();
             src_point_id < point_end;
             ++src_point_id)
        {
            Point&     src_point = m_source.points[src_point_id];
            Polygon_id new_polygon_id = m_destination.make_polygon();

            for (Point_corner_id point_corner_id = src_point.first_point_corner_id,
                point_corner_end = src_point.first_point_corner_id + src_point.corner_count;
                point_corner_id < point_corner_end;
                ++point_corner_id)
            {
                Corner_id  src_corner_id  = m_source.point_corners[point_corner_id];
                Corner&    src_corner     = m_source.corners[src_corner_id];
                Polygon_id src_polygon_id = src_corner.polygon_id;
                make_new_corner_from_polygon_centroid(new_polygon_id, src_polygon_id);
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

auto dual(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry
{
    erhe::geometry::Geometry result(fmt::format("dual({})", source.name()));
    Conway_dual_operator operation(source, result);
    return result;
}


} // namespace erhe::geometry::operation
