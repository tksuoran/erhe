#include "erhe/geometry/operation/clone.hpp"

namespace erhe::geometry::operation
{

Clone::Clone(Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    for (Point_id src_point_id = 0,
         point_end = source.point_count();
         src_point_id < point_end;
         ++src_point_id)
    {
        make_new_point_from_point(src_point_id);
    }

    for (Polygon_id src_polygon_id = 0,
         polygon_end = source.polygon_count();
         src_polygon_id < polygon_end;
         ++src_polygon_id)
    {
        auto new_polygon_id = make_new_polygon_from_polygon(src_polygon_id);
        add_polygon_corners(new_polygon_id, src_polygon_id);
    }

    build_destination_edges_with_sourcing();
    interpolate_all_property_maps();
}

} // namespace erhe::geometry::operation
