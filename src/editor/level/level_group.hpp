#pragma once

#include "level/kit_shape.hpp"

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <span>

// Scene-free parts of the group tools (doc/editor/level_tools.md "Groups", D3.8).

namespace editor {

// A local axis of the group node; the mirror plane goes through its origin with this axis as normal.
enum class Mirror_axis : unsigned int {
    x = 0,
    y,
    z
};

[[nodiscard]] auto c_str(Mirror_axis axis) -> const char*;

// The unit vector of the axis.
[[nodiscard]] auto get_axis_vector(Mirror_axis axis) -> glm::vec3;

// A world transform: translation, proper rotation and scale.
class Group_pose
{
public:
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale   {1.0f};
};

// Whether a mirrored pose shows the exact mirror image: the shape is symmetric about the plane used.
enum class Mirror_image : unsigned int {
    exact = 0,
    not_exact
};

class Mirrored_pose
{
public:
    Group_pose       pose;
    Kit_mirror_plane plane{Kit_mirror_plane::x}; // the plane in the pose's own frame composed with the reflection
    Mirror_image     image{Mirror_image::exact};
};

// D3.8: the position reflected across the plane through plane_point with unit normal plane_normal; the rotation
// the reflection composed with the reflection in the pose's own frame across the symmetry plane whose normal is
// closest to the plane's (the first on a tie), or across local x and not exact when there is none; scale kept.
[[nodiscard]] auto mirror_pose(
    const Group_pose&                  pose,
    const glm::vec3&                   plane_point,
    const glm::vec3&                   plane_normal,
    std::span<const Kit_mirror_plane>  symmetry_planes
) -> Mirrored_pose;

// The center of the base of the bounds: the origin of a new group node (D3.8).
[[nodiscard]] auto get_group_origin(const erhe::math::Aabb& bounds) -> glm::vec3;

} // namespace editor
