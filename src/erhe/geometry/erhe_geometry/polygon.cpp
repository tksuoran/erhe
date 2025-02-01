#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/property_map.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_profile/profile.hpp"

#include <glm/gtx/matrix_operation.hpp>

#include <fmt/format.h>

#include <cmath>
#include <sstream>

namespace erhe::geometry {


#if 0
auto Polygon::compute_edge_midpoint(
    const Geometry&                          geometry,
    const Property_map<Point_id, glm::vec3>& point_locations,
    const uint32_t                           corner_offset
) const -> glm::vec3
{
    if (corner_count >= 2) {
        const auto      polygon_corner_id_a = first_polygon_corner_id + (corner_offset       % corner_count);
        const auto      polygon_corner_id_b = first_polygon_corner_id + ((corner_offset + 1) % corner_count);
        const Corner_id corner0_id          = geometry.polygon_corners[polygon_corner_id_a];
        const Corner_id corner1_id          = geometry.polygon_corners[polygon_corner_id_b];
        const Corner&   corner0             = geometry.corners[corner0_id];
        const Corner&   corner1             = geometry.corners[corner1_id];
        const Point_id  a                   = corner0.point_id;
        const Point_id  b                   = corner1.point_id;
        if (point_locations.has(a) && point_locations.has(b)) {
            const glm::vec3 pos_a    = point_locations.get(a);
            const glm::vec3 pos_b    = point_locations.get(b);
            const glm::vec3 midpoint = (pos_a + pos_b) / 2.0f;
            return midpoint;
        }
    }
    log_geometry->warn("Could not compute edge midpoint for polygon with less than three corners");
    return glm::vec3{0.0f, 0.0f, 0.0f};
}
#endif

#if 0
void Polygon::reverse(Geometry& geometry)
{
    const auto last_polygon_corner_id = first_polygon_corner_id + corner_count;
    std::reverse(
        &geometry.polygon_corners[first_polygon_corner_id],
        &geometry.polygon_corners[last_polygon_corner_id]
    );
}

auto Polygon::format_points(const Geometry& geometry) const -> std::string
{
    std::stringstream ss;
    for (uint32_t i = 0; i < corner_count; ++i) {
        const Polygon_corner_id polygon_corner_id = first_polygon_corner_id + i;
        const Corner_id         corner_id         = geometry.polygon_corners[polygon_corner_id];
        const Corner&           corner            = geometry.corners[corner_id];
        ss << fmt::format("{} ", corner.point_id);
    }
    return ss.str();
}

auto Polygon::format_corners(const Geometry& geometry) const -> std::string
{
    std::stringstream ss;
    for (uint32_t i = 0; i < corner_count; ++i) {
        const Polygon_corner_id polygon_corner_id = first_polygon_corner_id + i;
        const Corner_id         corner_id         = geometry.polygon_corners[polygon_corner_id];
        ss << fmt::format("{} ", corner_id);
    }
    return ss.str();
}
#endif


} // namespace erhe::geometry
