#include "erhe/geometry/operation/triangulate.hpp"
#include "erhe/geometry/geometry.hpp"

#include "Tracy.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace erhe::geometry::operation
{

Triangulate::Triangulate(Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ZoneScoped;

    make_points_from_points();
    make_polygon_centroids();

    source.for_each_polygon([&](auto& i)
    {
        if (i.polygon.corner_count == 3)
        {
            Polygon_id new_polygon_id = make_new_polygon_from_polygon(i.polygon_id);
            add_polygon_corners(new_polygon_id, i.polygon_id);
            return;
        }

        i.polygon.for_each_corner_neighborhood(source, [&](auto& j)
        {
            Polygon_id new_polygon_id = destination.make_polygon();
            make_new_corner_from_polygon_centroid(new_polygon_id, i.polygon_id);
            make_new_corner_from_corner          (new_polygon_id, j.corner_id);
            make_new_corner_from_corner          (new_polygon_id, j.next_corner_id);
        });
    });

    post_processing();
}

auto triangulate(Geometry& source) -> Geometry
{
    Geometry result(fmt::format("triangulate({})", source.name));
    Triangulate operation(source, result);
    return result;
}


} // namespace erhe::geometry::operation
