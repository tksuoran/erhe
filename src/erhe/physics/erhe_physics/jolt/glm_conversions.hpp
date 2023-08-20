#pragma once

#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Color.h>

namespace erhe::physics
{

[[nodiscard]] inline auto from_jolt(const JPH::Vec3 v) -> glm::vec3
{
    return glm::vec3{
        v.GetX(),
        v.GetY(),
        v.GetZ()
    };
}

[[nodiscard]] inline auto from_jolt(const JPH::Vec4 v) -> glm::vec4
{
    return glm::vec4{
        v.GetX(),
        v.GetY(),
        v.GetZ(),
        v.GetW()
    };
}

[[nodiscard]] inline auto from_jolt(JPH::Float3 v) -> glm::vec3
{
    return glm::vec3{
        v.x,
        v.y,
        v.z
    };
}

[[nodiscard]] inline auto from_jolt(JPH::ColorArg c) -> glm::vec4
{
    return glm::vec4{
        c.r,
        c.g,
        c.b,
        c.a
    };
}

[[nodiscard]] inline auto from_jolt(const JPH::Quat q) -> glm::quat
{
#if defined(GLM_FORCE_QUAT_DATA_XYZW)
    return glm::quat{
        q.GetX(),
        q.GetY(),
        q.GetZ(),
        q.GetW()
    };
#else
    return glm::quat{
        q.GetW(),
        q.GetX(),
        q.GetY(),
        q.GetZ(),
    };
#endif
}

[[nodiscard]] inline auto to_jolt(const glm::vec3 v) -> JPH::Vec3
{
    return JPH::Vec3{v.x, v.y, v.z};
}

[[nodiscard]] inline auto to_jolt(const glm::vec4 v) -> JPH::Vec4
{
    return JPH::Vec4{v.x, v.y, v.z, v.w};
}

[[nodiscard]] inline auto to_jolt(const glm::quat q) -> JPH::Quat
{
    const auto nq = glm::normalize(q);
    const auto jq = JPH::Quat{nq.x, nq.y, nq.z, nq.w};
    ERHE_VERIFY(jq.IsNormalized());
    return jq;
}

[[nodiscard]] inline auto from_jolt(const JPH::Mat44 m) -> glm::mat4
{
    return glm::mat4{
        m(0, 0), m(1, 0), m(2, 0), m(3, 0),
        m(0, 1), m(1, 1), m(2, 1), m(3, 1),
        m(0, 2), m(1, 2), m(2, 2), m(3, 2),
        m(0, 3), m(1, 3), m(2, 3), m(3, 3)
    };
}

[[nodiscard]] inline auto to_jolt(const glm::mat4 m) -> JPH::Mat44
{
    return JPH::Mat44{
        to_jolt(m[0]),
        to_jolt(m[1]),
        to_jolt(m[2]),
        to_jolt(m[3])
    };
}

} // namespace erhe::physics
