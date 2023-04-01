#include "erhe/geometry/operation/dual.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/toolkit/profile.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace erhe::geometry::operation
{

Dual::Dual(Geometry& source, Geometry& destination, bool post_process)
    : Geometry_operation{source, destination}
{
    ERHE_PROFILE_FUNCTION();

    make_polygon_centroids();

    // New faces from old points, new face corner for each old point corner
    source.for_each_point_const([&](auto& i)
    {
        const Polygon_id new_polygon_id = destination.make_polygon();

        i.point.for_each_corner_const(source, [&](auto& j)
        {
            make_new_corner_from_polygon_centroid(new_polygon_id, j.corner.polygon_id);
        });
    });

    if (post_process) {
        post_processing();
    }
}

auto dual(Geometry& source) -> Geometry
{
    return Geometry{
        fmt::format("dual({})", source.name),
        [&source](auto& result)
        {
            Dual operation{source, result};
        }
    };
}


} // namespace erhe::geometry::operation
