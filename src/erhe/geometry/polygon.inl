#pragma once

//#include "erhe/toolkit/profile.hpp"
#ifndef ERHE_PROFILE_FUNCTION
#   define ERHE_PROFILE_FUNCTION()
#   define ERHE_PROFILE_FUNCTION_DUMMY
#endif

namespace erhe::geometry
{

template <typename T>
void Polygon::copy_to_corners(
    const Polygon_id                   this_polygon_id,
    const Geometry&                    geometry,
    Property_map<Corner_id, T>&        corner_attribute,
    const Property_map<Polygon_id, T>& polygon_attribute
) const
{
    ERHE_PROFILE_FUNCTION();

    const T polygon_value = polygon_attribute.get(this_polygon_id);
    for_each_corner_const(
        geometry,
        [&corner_attribute, polygon_value](auto& i) {
            corner_attribute.put(i.corner_id, polygon_value);
        }
    );
}

template <typename T>
void Polygon::smooth_normalize(
    const Geometry&                            geometry,
    Property_map<Corner_id, T>&                corner_attribute,
    const Property_map<Polygon_id, T>&         polygon_attribute,
    const Property_map<Polygon_id, glm::vec3>& polygon_normals,
    const float                                cos_max_smoothing_angle
) const
{
    ERHE_PROFILE_FUNCTION();

    for (
        Polygon_corner_id polygon_corner_id = first_polygon_corner_id,
        end = first_polygon_corner_id + corner_count;
        polygon_corner_id < end;
        ++polygon_corner_id
    ) {
        const Corner_id corner_id = geometry.polygon_corners[polygon_corner_id];
        const Corner&   corner    = geometry.corners[corner_id];
        corner.smooth_normalize(
            corner_id,
            geometry,
            corner_attribute,
            polygon_attribute,
            polygon_normals,
            cos_max_smoothing_angle
        );
    }
}

template <typename T>
void Polygon::smooth_average(
    const Geometry&                           geometry,
    Property_map<Corner_id, T>&               new_corner_attribute,
    const Property_map<Corner_id, T>&         old_corner_attribute,
    const Property_map<Corner_id, glm::vec3>& normer_normals,
    const Property_map<Point_id, glm::vec3>&  point_normals
) const
{
    ERHE_PROFILE_FUNCTION();

    for (
        Polygon_corner_id polygon_corner_id = first_polygon_corner_id,
        end = first_polygon_corner_id + corner_count;
        polygon_corner_id < end;
        ++polygon_corner_id
    ) {
        const Corner_id corner_id = geometry.polygon_corners[polygon_corner_id];
        const Corner&   corner    = geometry.corners[corner_id];
        corner.smooth_average(
            corner_id,
            geometry,
            new_corner_attribute,
            old_corner_attribute,
            normer_normals,
            point_normals
        );
    }
}

} // namespace erhe::geometry

#ifdef ERHE_PROFILE_FUNCTION_DUMMY
#   undef ERHE_PROFILE_FUNCTION
#   undef ERHE_PROFILE_FUNCTION_DUMMY
#endif
