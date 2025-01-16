#if 0
#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <string>

namespace erhe::geometry {
    class Polygon;
}

namespace erhe::geometry::operation {

class Weld : public Geometry_operation
{
public:
    Weld(const Geometry& source, Geometry& destination);

private:
    void sort_points_by_location             ();
    void find_point_merge_candidates         ();
    void rotate_polygons_to_least_point_first();
    void sort_polygons                       ();
    void scan_for_equal_and_opposite_polygons();
    void mark_used_points                    ();
    void count_used_points                   ();

    auto format_polygon_points(const Polygon& polygon) const -> std::string;

    float                   m_max_distance;
    uint32_t                m_used_point_count;
    std::vector<Point_id>   m_point_id_merge_candidates;
    std::vector<bool>       m_point_id_used            ;
    std::vector<Polygon_id> m_polygon_id_sorted        ;
    std::vector<Polygon_id> m_polygon_id_remove        ;
};

[[nodiscard]] auto weld(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
#endif