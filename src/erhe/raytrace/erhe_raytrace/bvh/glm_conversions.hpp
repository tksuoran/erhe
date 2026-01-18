#pragma once

#include <bvh/v2/vec.h>
#include <glm/glm.hpp>

namespace erhe::raytrace {

[[nodiscard]] inline auto from_bvh(const bvh::v2::Vec<float, 3> v) -> glm::vec3
{
    return glm::vec3{
        v[0],
        v[1],
        v[2]
    };
}

[[nodiscard]] inline auto to_bvh(const glm::vec3 v) -> bvh::v2::Vec<float, 3>
{
    return bvh::v2::Vec<float, 3>{v.x, v.y, v.z};
}

} // namespace erhe::physics
