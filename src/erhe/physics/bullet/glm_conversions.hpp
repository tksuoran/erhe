#pragma once

#include "erhe/log/log.hpp"

#include <glm/glm.hpp>

#include <LinearMath/btVector3.h>

namespace erhe::physics
{

inline auto to_glm(const btVector3 v) -> glm::vec3
{
    return glm::vec3{v.x(), v.y(), v.z()};
}

inline auto from_glm(const glm::vec3 v) -> btVector3
{
    return btVector3{v.x, v.y, v.z};
}

inline auto to_glm(const btMatrix3x3 m) -> glm::mat3
{
    //return glm::mat3{
    //    m.getColumn(0).x(), m.getColumn(1).x(), m.getColumn(2).x(),
    //    m.getColumn(0).y(), m.getColumn(1).y(), m.getColumn(2).y(),
    //    m.getColumn(0).z(), m.getColumn(1).z(), m.getColumn(2).z()
    //};

    return glm::mat3{
        m.getColumn(0).x(), m.getColumn(0).y(), m.getColumn(0).z(),
        m.getColumn(1).x(), m.getColumn(1).y(), m.getColumn(1).z(),
        m.getColumn(2).x(), m.getColumn(2).y(), m.getColumn(2).z()
    };
}

inline auto from_glm(const glm::mat3 m) -> btMatrix3x3
{
    return btMatrix3x3{
        m[0][0], m[1][0], m[2][0],
        m[0][1], m[1][1], m[2][1],
        m[0][2], m[1][2], m[2][2]
    };

    //return btMatrix3x3{
    //    m[0][0], m[0][1], m[0][2], 
    //    m[1][0], m[1][1], m[1][2], 
    //    m[2][0], m[2][1], m[2][2]
    //};
}

} // namespace erhe::physics
