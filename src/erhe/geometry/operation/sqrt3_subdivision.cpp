#include "erhe/geometry/operation/sqrt3_subdivision.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace erhe::geometry::operation
{

//  Sqrt(3): Replace edges with two triangles               
//  For each corner in the old polygon, add one triangle    
//  (centroid, corner, opposite centroid)                   
//                                                          
//  Centroids:                                              
//                                                          
//  (1) q := 1/3 (p_i + p_j + p_k)                          
//                                                          
//  Refine old vertices:                                    
//                                                          
//  (2) S(p) := (1 - alpha_n) p + alpha_n 1/n SUM p_i       
//                                                          
//  (6) alpha_n = (4 - 2 cos(2Pi/n)) / 9                    
Sqrt3_subdivision::Sqrt3_subdivision(Geometry& src, Geometry& destination)
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
            Point& src_point = m_source.points[src_point_id];
            float alpha            = (4.0f - 2.0f * std::cos(2.0f * glm::pi<float>() / src_point.corner_count)) / 9.0f;
            float alpha_per_n      = alpha / static_cast<float>(src_point.corner_count);
            float alpha_complement = 1.0f - alpha;

            Point_id new_point = make_new_point_from_point(alpha_complement, src_point_id);
            add_point_ring(new_point, alpha_per_n, src_point_id);
        }
    }

    make_polygon_centroids();

    {
        ZoneScopedN("Subdivide");

        for (Polygon_id src_polygon_id = 0,
             polygon_end = m_source.polygon_count();
             src_polygon_id < polygon_end;
             ++src_polygon_id)
        {
            Polygon& src_polygon = m_source.polygons[src_polygon_id];

            for (uint32_t i = 0; i < src_polygon.corner_count; ++i)
            {
                Polygon_corner_id src_polygon_corner_id      = src_polygon.first_polygon_corner_id + i;
                Polygon_corner_id src_polygon_next_corner_id = src_polygon.first_polygon_corner_id + (i + 1) % src_polygon.corner_count;
                Corner_id src_corner_id      = m_source.polygon_corners[src_polygon_corner_id];
                Corner_id src_next_corner_id = m_source.polygon_corners[src_polygon_next_corner_id];
                Corner&   src_corner         = m_source.corners[src_corner_id];
                Corner&   src_next_corner    = m_source.corners[src_next_corner_id];
                Point_id  src_point_id       = src_corner.point_id;
                Point_id  src_next_point_id  = src_next_corner.point_id;

                auto edge_opt = m_source.find_edge(src_point_id, src_next_point_id);
                if (!edge_opt.has_value())
                {
                    continue;
                }
                const auto& edge = edge_opt.value();
                Polygon_id opposite_polygon_id = src_polygon_id;
                for (Edge_polygon_id edge_polygon_id = edge.first_edge_polygon_id,
                     end = edge.first_edge_polygon_id + edge.polygon_count;
                     edge_polygon_id < end;
                     ++edge_polygon_id)
                {
                    opposite_polygon_id = m_source.edge_polygons[edge_polygon_id];
                    if (opposite_polygon_id != src_polygon_id)
                    {
                        break;
                    }
                }
                if (opposite_polygon_id == src_polygon_id)
                {
                    continue;
                }
                Polygon_id new_polygon_id = make_new_polygon_from_polygon(src_polygon_id);
                make_new_corner_from_polygon_centroid(new_polygon_id, src_polygon_id);
                make_new_corner_from_corner(new_polygon_id, src_corner_id);
                make_new_corner_from_polygon_centroid(new_polygon_id, opposite_polygon_id);
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

auto sqrt3_subdivision(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry
{
    erhe::geometry::Geometry result(fmt::format("sqrt3({})", source.name()));
    Sqrt3_subdivision operation(source, result);
    return result;
}


} // namespace erhe::geometry::operation
