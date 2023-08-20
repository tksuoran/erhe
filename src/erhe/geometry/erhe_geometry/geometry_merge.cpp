#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"

#include <glm/glm.hpp>

namespace erhe::geometry
{

void Geometry::merge(Geometry& other, const glm::mat4 transform)
{
    ERHE_PROFILE_FUNCTION();

    // Append corners
    const Corner_id combined_corner_count = get_corner_count() + other.get_corner_count();
    if (corners.size() < combined_corner_count) {
        corners.resize(combined_corner_count);
    }

    for (
        Corner_id corner_id = 0, end = other.get_corner_count();
        corner_id < end;
        ++corner_id
    ) {
        const Corner_id new_corner_id = corner_id + m_next_corner_id;
        const Corner&   src_corner    = other.corners[corner_id];
        Corner&         dst_corner    = corners[new_corner_id];
        dst_corner.point_id   = src_corner.point_id   + m_next_point_id;
        dst_corner.polygon_id = src_corner.polygon_id + m_next_polygon_id;
    }

    // Append points
    const Point_id combined_point_count = get_point_count() + other.get_point_count();
    if (points.size() < combined_point_count) {
        points.resize(combined_point_count);
    }

    for (
        Point_id point_id = 0, end = other.get_point_count();
        point_id < end;
        ++point_id
    ) {
        const Point_id dst_point_id = point_id + m_next_point_id;
        const Point&   src_point    = other.points[point_id];;
        Point&         dst_point    = points[dst_point_id];
        dst_point.first_point_corner_id = src_point.first_point_corner_id + m_next_point_corner_reserve;
        dst_point.corner_count          = src_point.corner_count;
        dst_point.reserved_corner_count = src_point.reserved_corner_count;
    }

    // Append point corners
    const Point_corner_id combined_point_corner_count = get_point_corner_count() + other.get_point_corner_count();
    if (point_corners.size() < combined_point_corner_count) {
        point_corners.resize(combined_point_corner_count);
    }
    for (
        Point_corner_id point_corner_id = 0, end = other.get_point_corner_count();
        point_corner_id < end;
        ++point_corner_id
    ) {
        Point_corner_id dst_point_corner_id = point_corner_id + m_next_point_corner_reserve;
        point_corners[dst_point_corner_id] = other.point_corners[point_corner_id] + m_next_corner_id;
    }

    // Append polygons
    const Polygon_id combined_polygon_count = m_next_polygon_id + other.get_polygon_count();
    if (polygons.size() < combined_polygon_count) {
        polygons.resize(combined_polygon_count);
    }

    for (
        Polygon_id polygon_id = 0, end = other.get_polygon_count();
        polygon_id < end;
        ++polygon_id
    ) {
        const Polygon_id dst_polygon_id = polygon_id + m_next_polygon_id;
        const Polygon&   src_polygon    = other.polygons[polygon_id];
        Polygon&         dst_polygon    = polygons[dst_polygon_id];
        dst_polygon.first_polygon_corner_id = src_polygon.first_polygon_corner_id + m_next_polygon_corner_id;
        dst_polygon.corner_count            = src_polygon.corner_count;
    }

    // Append polygon corners
    const Polygon_corner_id combined_polygon_corner_count = get_polygon_corner_count() + other.get_polygon_corner_count();
    if (polygon_corners.size() < combined_polygon_corner_count) {
        polygon_corners.resize(combined_polygon_corner_count);
    }

    for (
        Polygon_corner_id polygon_corner_id = 0, end = other.get_polygon_corner_count();
        polygon_corner_id < end;
        ++polygon_corner_id
    ) {
        const Polygon_corner_id dst_polygon_corner_id = polygon_corner_id + m_next_polygon_corner_id;
        polygon_corners[dst_polygon_corner_id]  = other.polygon_corners[polygon_corner_id] + m_next_corner_id;
    }

    // Merging only works with trimmed attribute maps
    other.corner_attributes ().trim(other.get_corner_count ());
    other.point_attributes  ().trim(other.get_point_count  ());
    other.polygon_attributes().trim(other.get_polygon_count());
    other.edge_attributes   ().trim(other.get_edge_count   ());
    m_corner_property_map_collection .trim(get_corner_count ());
    m_point_property_map_collection  .trim(get_point_count  ());
    m_polygon_property_map_collection.trim(get_polygon_count());
    m_edge_property_map_collection   .trim(get_edge_count   ());

    other.corner_attributes ().merge_to(m_corner_property_map_collection , transform);
    other.point_attributes  ().merge_to(m_point_property_map_collection  , transform);
    other.polygon_attributes().merge_to(m_polygon_property_map_collection, transform);
    other.edge_attributes   ().merge_to(m_edge_property_map_collection   , transform);

    m_next_corner_id            += other.get_corner_count();
    m_next_point_id             += other.get_point_count();
    m_next_point_corner_reserve += other.get_point_corner_count();
    m_next_polygon_id           += other.get_polygon_count();
    m_next_polygon_corner_id    += other.get_polygon_corner_count();
}

}
