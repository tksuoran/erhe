#pragma once

//#include "erhe_profile/profile.hpp"
#ifndef ERHE_PROFILE_FUNCTION
#   define ERHE_PROFILE_FUNCTION()
#   define ERHE_PROFILE_FUNCTION_DUMMY
#endif

namespace erhe::geometry {

template <typename T>
void Geometry::smooth_normalize(
    Property_map<Corner_id, T>&                corner_attribute,
    const Property_map<Polygon_id, T>&         polygon_attribute,
    const Property_map<Polygon_id, glm::vec3>& polygon_normals,
    const float                                max_smoothing_angle_radians
) const
{
    ERHE_PROFILE_FUNCTION();

    const float cos_max_smoothing_angle = std::cos(max_smoothing_angle_radians);

    corner_attribute.clear();
    for_each_polygon_const(
        [&](auto& i) {
            if (max_smoothing_angle_radians == 0.0f) {
                i.polygon.copy_to_corners(
                    i.polygon_id,
                    *this,
                    corner_attribute,
                    polygon_attribute
                );
            } else {
                i.polygon.smooth_normalize(
                    *this,
                    corner_attribute,
                    polygon_attribute,
                    polygon_normals,
                    cos_max_smoothing_angle
                );
            }
        }
    );
}


template <typename T>
void Geometry::smooth_average(
    Property_map<Corner_id, T>&                smoothed_corner_attribute,
    const Property_map<Corner_id, T>&          corner_attribute,
    const Property_map<Corner_id, glm::vec3>&  corner_normals,
    const Property_map<Polygon_id, glm::vec3>& point_normals
) const
{
    ERHE_PROFILE_FUNCTION();

    for_each_polygon(
        [&](auto& i) {
            i.polygon.smooth_average(
                *this,
                smoothed_corner_attribute,
                corner_attribute,
                corner_normals,
                point_normals
            );
        }
    );
}

} // namespace erhe::geometry

#ifdef ERHE_PROFILE_FUNCTION_DUMMY
#   undef ERHE_PROFILE_FUNCTION
#   undef ERHE_PROFILE_FUNCTION_DUMMY
#endif
