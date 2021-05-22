#include "erhe/geometry/operation/sqrt3_subdivision.hpp"
#include "erhe/geometry/geometry.hpp"

#include "Tracy.hpp"

#include <fmt/format.h>
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

    source.for_each_point([&](auto& i)
    {
        float    alpha            = (4.0f - 2.0f * std::cos(2.0f * glm::pi<float>() / i.point.corner_count)) / 9.0f;
        float    alpha_per_n      = alpha / static_cast<float>(i.point.corner_count);
        float    alpha_complement = 1.0f - alpha;
        Point_id new_point        = make_new_point_from_point(alpha_complement, i.point_id);
        add_point_ring(new_point, alpha_per_n, i.point_id);
    });

    make_polygon_centroids();

    source.for_each_polygon([&](auto& i)
    {
        i.polygon.for_each_corner_neighborhood(source, [&](auto& j)
        {
            Point_id src_point_id      = j.corner.point_id;
            Point_id src_next_point_id = j.next_corner.point_id;

            auto edge_opt = source.find_edge(src_point_id, src_next_point_id);
            if (!edge_opt.has_value())
            {
                return;
            }
            auto& edge = edge_opt.value();
            Polygon_id opposite_polygon_id = i.polygon_id;
            edge.for_each_polygon(source, [&](auto& k)
            {
                if (k.polygon_id != i.polygon_id)
                {
                    opposite_polygon_id = k.polygon_id;
                    return k.break_iteration();
                }
            });
            if (opposite_polygon_id == i.polygon_id)
            {
                return;
            }
            Polygon_id new_polygon_id = make_new_polygon_from_polygon(i.polygon_id);
            make_new_corner_from_polygon_centroid(new_polygon_id, i.polygon_id);
            make_new_corner_from_corner          (new_polygon_id, j.corner_id);
            make_new_corner_from_polygon_centroid(new_polygon_id, opposite_polygon_id);
        });
    });

    post_processing();
}

auto sqrt3_subdivision(Geometry& source) -> Geometry
{
    Geometry result(fmt::format("sqrt3({})", source.name));
    Sqrt3_subdivision operation(source, result);
    return result;
}


} // namespace erhe::geometry::operation
