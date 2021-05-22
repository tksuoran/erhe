#pragma once

#include "Tracy.hpp"

namespace erhe::geometry
{

template <typename T>
void Corner::smooth_normalize(Corner_id                                  this_corner_id,
                              const Geometry&                            geometry,
                              Property_map<Corner_id, T>&                corner_attribute,
                              const Property_map<Polygon_id, T>&         polygon_attribute,
                              const Property_map<Polygon_id, glm::vec3>& polygon_normals,
                              float                                      cos_max_smoothing_angle) const
{
    ZoneScoped;

    if (polygon_normals.has(polygon_id) == false)
    {
        return;
    }

    auto polygon_normal = polygon_normals.get(polygon_id);
    T    polygon_value  = polygon_attribute.get(polygon_id);
    T    corner_value   = polygon_value;

    size_t point_corner_count{0};
    size_t participant_count{0};

    const Point& point = geometry.points[point_id];
    point.for_each_corner_const(geometry, [&](const auto& i)
    {
        ++point_corner_count;
        Polygon_id     neighbor_polygon_id = i.corner.polygon_id;
        const Polygon& neighbor_polygon    = geometry.polygons[polygon_id];

        if ((polygon_id != neighbor_polygon_id) &&
            polygon_normals.has(neighbor_polygon_id) &&
            polygon_attribute.has(neighbor_polygon_id) &&
            (neighbor_polygon.corner_count > 2))
        {
            auto neighbor_normal = polygon_normals.get(neighbor_polygon_id);
            float cos_angle = glm::dot(polygon_normal, neighbor_normal);
            if (cos_angle > 1.0f)
            {
                cos_angle = 1.0f;
            }

            if (cos_angle < -1.0f)
            {
                cos_angle = -1.0f;
            }

            // Smaller cosine means larger angle means less sharp
            // Higher cosine means lesser angle means more sharp
            // Cosine == 1 == maximum sharpness
            // Cosine == -1 == minimum sharpness (flat)
            if (cos_angle <= cos_max_smoothing_angle)
            {
                corner_value += polygon_attribute.get(neighbor_polygon_id);
                ++participant_count;
            }
        }
    });

    corner_value = normalize(corner_value);
    corner_attribute.put(this_corner_id, corner_value);
}

template <typename T>
void Corner::smooth_average(Corner_id                                 this_corner_id,
                            const Geometry&                           geometry,
                            Property_map<Corner_id, T>&               new_corner_attribute,
                            const Property_map<Corner_id, T>&         old_corner_attribute,
                            const Property_map<Corner_id, glm::vec3>& corner_normals,
                            const Property_map<Point_id, glm::vec3>&  point_normals) const
{
    ZoneScoped;

    bool has_corner_normal = corner_normals.has(this_corner_id);
    if (!has_corner_normal && !point_normals.has(point_id))
    {
        return;
    }

    auto corner_normal = has_corner_normal ? corner_normals.get(this_corner_id)
                                           : point_normals.get(point_id);
    T corner_value{};

    size_t participant_count{0};
    const Point& point = geometry.points[point_id];
    point.for_each_corner_const([&](const auto& i)
    {
        if (!has_corner_normal || (corner_normals.get(i.corner_id) == corner_normal))
        {
            if (old_corner_attribute.has(i.corner_id))
            {
                corner_value += old_corner_attribute.get(i.corner_id);
                ++participant_count;
            }
        }
    });
    VERIFY(participant_count >= 1);

    corner_value = corner_value / static_cast<float>(participant_count);
    new_corner_attribute.put(this_corner_id, corner_value);
}

} // namespace erhe::geometry
