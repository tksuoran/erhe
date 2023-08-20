#include "erhe_raytrace/ray.hpp"

namespace erhe::raytrace
{

auto Ray::transform(
    const glm::mat4& matrix
) const -> Ray
{
    return Ray{
        .origin    = glm::vec3{matrix * glm::vec4{origin, 1.0}},
        .t_near    = t_near,
        .direction = glm::vec3{matrix * glm::vec4{direction, 0.0}},
        .time      = time,
        .t_far     = t_far,
        .mask      = mask,
        .id        = id,
        .flags     = flags
    };
}

} // namespace
