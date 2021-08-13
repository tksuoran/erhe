#pragma once

#include <embree3/rtcore.h>
#include <glm/glm.hpp>

namespace erhe::raytrace
{

inline auto ray(glm::vec3 origin, glm::vec3 direction) -> RTCRay
{
}
auto ray(glm::vec3 origin, glm::vec3 direction, float tnear, float tfar) -> RTCRay;
auto ray(glm::vec3 origin, glm::vec3 direction, float tnear, float tfar, unsigned int id) -> RTCRay;
    float org_x;        // x coordinate of ray origin
    float org_y;        // y coordinate of ray origin
    float org_z;        // z coordinate of ray origin
    float tnear;        // start of ray segment

    float dir_x;        // x coordinate of ray direction
    float dir_y;        // y coordinate of ray direction
    float dir_z;        // z coordinate of ray direction
    float time;         // time of this ray for motion blur

    float tfar;         // end of ray segment (set to hit distance)
    unsigned int mask;  // ray mask
    unsigned int id;    // ray ID
    unsigned int flags; // ray flags
};

} // namespace erhe::raytrace
