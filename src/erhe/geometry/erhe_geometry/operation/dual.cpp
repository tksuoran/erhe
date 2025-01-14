#include "erhe_geometry/operation/dual.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>

namespace erhe::geometry::operation {

Dual::Dual(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
    ERHE_PROFILE_FUNCTION();

    make_polygon_centroids();

    // New faces from old points, new face corner for each old point corner
    source.for_each_point_const([&](auto& i) {
        const Polygon_id new_polygon_id = destination.make_polygon();

        i.point.for_each_corner_const(source, [&](auto& j) {
            make_new_corner_from_polygon_centroid(new_polygon_id, j.corner.polygon_id);
        });
    });

    post_processing();
}

auto dual(const Geometry& source) -> Geometry
{
    return Geometry{
        fmt::format("dual({})", source.name),
        [&source](auto& result) {
            Dual operation{source, result};
        }
    };
}


} // namespace erhe::geometry::operation
