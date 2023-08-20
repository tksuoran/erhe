#include "erhe_geometry/operation/kis.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>

namespace erhe::geometry::operation
{

Kis::Kis(Geometry& src, Geometry& destination)
    : Geometry_operation{src, destination}
{
    ERHE_PROFILE_FUNCTION();

    make_points_from_points();
    make_polygon_centroids();

    source.for_each_polygon_const([&](auto& i) {
        i.polygon.for_each_corner_neighborhood_const(source, [&](auto& j) {
            const Polygon_id new_polygon_id = destination.make_polygon();
            make_new_corner_from_polygon_centroid(new_polygon_id, i.polygon_id);
            make_new_corner_from_corner          (new_polygon_id, j.corner_id);
            make_new_corner_from_corner          (new_polygon_id, j.next_corner_id);
        });
    });

    post_processing();
}

auto kis(Geometry& source) -> Geometry
{
    return Geometry{
        fmt::format("kis({})", source.name),
        [&source](auto& result) {
            Kis operation{source, result};
        }
    };
}


} // namespace erhe::geometry::operation
