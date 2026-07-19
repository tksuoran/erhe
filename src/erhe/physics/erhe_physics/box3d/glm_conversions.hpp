#pragma once

#include "erhe_physics/transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <box3d/box3d.h>

namespace erhe::physics {

// Box3D math conventions (verified against box3d/math_functions.h):
//  - b3Vec3   : { float x, y, z }
//  - b3Quat   : { b3Vec3 v; float s }  -- xyzw storage, s is the scalar (w) part
//  - b3Matrix3: { b3Vec3 cx, cy, cz }  -- column major, same as glm::mat3
//  - b3Transform : { b3Vec3 p; b3Quat q }

[[nodiscard]] inline auto from_box3d(const b3Vec3 v) -> glm::vec3
{
    return glm::vec3{v.x, v.y, v.z};
}

[[nodiscard]] inline auto to_box3d(const glm::vec3 v) -> b3Vec3
{
    return b3Vec3{v.x, v.y, v.z};
}

[[nodiscard]] inline auto from_box3d(const b3Quat q) -> glm::quat
{
    // glm::quat's (w, x, y, z) constructor order is independent of
    // GLM_FORCE_QUAT_DATA_XYZW, which only changes the storage layout.
    return glm::quat{q.s, q.v.x, q.v.y, q.v.z};
}

[[nodiscard]] inline auto to_box3d(const glm::quat q) -> b3Quat
{
    const glm::quat normalized_q = glm::normalize(q);
    return b3Quat{b3Vec3{normalized_q.x, normalized_q.y, normalized_q.z}, normalized_q.w};
}

[[nodiscard]] inline auto from_box3d(const b3Matrix3 m) -> glm::mat3
{
    return glm::mat3{
        from_box3d(m.cx),
        from_box3d(m.cy),
        from_box3d(m.cz)
    };
}

[[nodiscard]] inline auto to_box3d(const glm::mat3 m) -> b3Matrix3
{
    return b3Matrix3{to_box3d(m[0]), to_box3d(m[1]), to_box3d(m[2])};
}

// erhe::physics::Transform stores an orthonormal basis (mat3) plus an origin.
[[nodiscard]] inline auto from_box3d(const b3Transform t) -> Transform
{
    return Transform{glm::mat3_cast(from_box3d(t.q)), from_box3d(t.p)};
}

[[nodiscard]] inline auto to_box3d(const Transform& t) -> b3Transform
{
    return b3Transform{to_box3d(t.origin), to_box3d(glm::quat_cast(t.basis))};
}

[[nodiscard]] inline auto make_box3d_transform(const glm::vec3 position, const glm::quat orientation) -> b3Transform
{
    return b3Transform{to_box3d(position), to_box3d(orientation)};
}

// Box3D returns the local rotational inertia as a 3x3 tensor; erhe's interface
// carries it in the upper-left 3x3 of a mat4.
[[nodiscard]] inline auto inertia_from_box3d(const b3Matrix3 m) -> glm::mat4
{
    return glm::mat4{from_box3d(m)};
}

[[nodiscard]] inline auto inertia_to_box3d(const glm::mat4 m) -> b3Matrix3
{
    return to_box3d(glm::mat3{m});
}

} // namespace erhe::physics
