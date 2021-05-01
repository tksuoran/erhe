#pragma once

#include "Tracy.hpp"

namespace erhe::geometry
{

template <typename T>
void Geometry::smooth_normalize(Property_map<Corner_id, T>&                corner_attribute,
                                const Property_map<Polygon_id, T>&         polygon_attribute,
                                const Property_map<Polygon_id, glm::vec3>& polygon_normals,
                                float                                      max_smoothing_angle_radians) const
{
    ZoneScoped;

    float cos_max_smoothing_angle = cos(max_smoothing_angle_radians);

    corner_attribute.clear();
    for (Polygon_id polygon_id = 0, end = polygon_count(); polygon_id < end; ++polygon_id)
    {
        const Polygon& polygon = polygons[polygon_id];
        if (max_smoothing_angle_radians == 0.0f)
        {
            polygon.copy_to_corners(polygon_id,
                                    *this,
                                    corner_attribute,
                                    polygon_attribute);
        }
        else
        {
            polygon.smooth_normalize(*this,
                                     corner_attribute,
                                     polygon_attribute,
                                     polygon_normals,
                                     cos_max_smoothing_angle);
        }
    }
}


template <typename T>
void Geometry::smooth_average(Property_map<Corner_id, T>&                smoothed_corner_attribute,
                              const Property_map<Corner_id, T>&          corner_attribute,
                              const Property_map<Corner_id, glm::vec3>&  corner_normals,
                              const Property_map<Polygon_id, glm::vec3>& point_normals) const
{
    ZoneScoped;

    for (auto& polygon : polygons)
    {
        polygon.smooth_average(*this,
                               smoothed_corner_attribute,
                               corner_attribute,
                               corner_normals,
                               point_normals);
    }
}

} // namespace erhe::geometry
