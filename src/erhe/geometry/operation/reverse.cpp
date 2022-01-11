#include "erhe/geometry/operation/reverse.hpp"
#include "erhe/geometry/geometry.hpp"

#include <fmt/format.h>

namespace erhe::geometry::operation
{

Reverse::Reverse(
    Geometry& source,
    Geometry& destination
)
    : Geometry_operation{source, destination}
{
    destination.points                               = source.points;
    destination.polygons                             = source.polygons;
    destination.edges                                = source.edges;
    destination.corners                              = source.corners;
    destination.point_corners                        = source.point_corners;
    destination.polygon_corners                      = source.polygon_corners;
    destination.m_next_corner_id                     = source.m_next_corner_id;
    destination.m_next_point_id                      = source.m_next_point_id;
    destination.m_next_polygon_id                    = source.m_next_polygon_id;
    destination.m_next_edge_id                       = source.m_next_edge_id;
    destination.m_next_point_corner_reserve          = source.m_next_point_corner_reserve;
    destination.m_next_polygon_corner_id             = source.m_next_polygon_corner_id;
    destination.m_serial                             = source.m_serial                            ;
    destination.m_serial_edges                       = source.m_serial_edges                      ;
    destination.m_serial_polygon_normals             = source.m_serial_polygon_normals            ;
    destination.m_serial_polygon_centroids           = source.m_serial_polygon_centroids          ;
    destination.m_serial_polygon_tangents            = source.m_serial_polygon_tangents           ;
    destination.m_serial_polygon_bitangents          = source.m_serial_polygon_bitangents         ;
    destination.m_serial_polygon_texture_coordinates = source.m_serial_polygon_texture_coordinates;
    destination.m_serial_point_normals               = source.m_serial_point_normals              ;
    destination.m_serial_point_tangents              = source.m_serial_point_tangents             ;
    destination.m_serial_point_bitangents            = source.m_serial_point_bitangents           ;
    destination.m_serial_point_texture_coordinates   = source.m_serial_point_texture_coordinates  ;
    destination.m_serial_smooth_point_normals        = source.m_serial_smooth_point_normals       ;
    destination.m_serial_corner_normals              = source.m_serial_corner_normals             ;
    destination.m_serial_corner_tangents             = source.m_serial_corner_tangents            ;
    destination.m_serial_corner_bitangents           = source.m_serial_corner_bitangents          ;
    destination.m_serial_corner_texture_coordinates  = source.m_serial_corner_texture_coordinates ;

    destination.m_next_edge_polygon_id            = source.m_next_edge_polygon_id;
    destination.m_point_property_map_collection   = source.m_point_property_map_collection  .clone();
    destination.m_corner_property_map_collection  = source.m_corner_property_map_collection .clone();
    destination.m_polygon_property_map_collection = source.m_polygon_property_map_collection.clone();
    destination.m_edge_property_map_collection    = source.m_edge_property_map_collection   .clone();

    destination.reverse_polygons();
}

auto reverse(Geometry& source) -> Geometry
{
    return Geometry{
        fmt::format("reverse({})", source.name),
        [&source](auto& result)
        {
            Reverse operation{source, result};
        }
    };
}

} // namespace erhe::geometry::operation
