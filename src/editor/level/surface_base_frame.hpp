#pragma once

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>

// The base face of a mesh's bounds for snap to surface (doc/editor/level_tools.md "Surface placement").
namespace editor {

// A transform in the scene root's space.
class Surface_pose
{
public:
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale   {1.0f};
};

// How a mesh's base face is chosen (U48).
enum class Surface_base_rule : unsigned int {
    authored_y_up = 0, // a kit piece, modelled Y-up: of its two Y faces the one the pose turns down (local -Y unless mirrored)
    from_pose          // any other mesh: the face the pose turns most along the snap direction
};

enum class Bounds_side : unsigned int {
    min = 0,
    max
};

// The face of the node-space bounds a mesh rests on, and its up axis from that face toward the opposite (top) face.
class Surface_base_frame
{
public:
    int         axis{1};                    // local axis across the base face
    Bounds_side side{Bounds_side::min};     // which end of that axis holds the base face
    int         u   {0};                    // the two in-face axes, ascending
    int         v   {2};
    glm::vec3   up_local{0.0f, 1.0f, 0.0f}; // unit; the start pose takes it to the world up axis of the piece
};

// from_pose: the face whose outward normal, taken to world space by the rotation and the scale's signs, is most along
// the direction; exact ties go to local -Y, then +Y, -X, +X, -Z, +Z.
[[nodiscard]] auto get_surface_base_frame(
    Surface_base_rule rule,
    const glm::quat&  rotation,
    const glm::vec3&  scale,
    const glm::vec3&  direction
) -> Surface_base_frame;

// Orient's base face once the surface normal under the line's base center is known: a kit piece keeps the line's face,
// from_pose takes the face whose outward normal is most against the surface normal (the least turn onto it).
[[nodiscard]] auto get_orient_frame(
    Surface_base_rule         rule,
    const glm::quat&          rotation,
    const glm::vec3&          scale,
    const Surface_base_frame& line_frame,
    const glm::vec3&          normal
) -> Surface_base_frame;

[[nodiscard]] inline auto is_same_face(const Surface_base_frame& lhs, const Surface_base_frame& rhs) -> bool
{
    return (lhs.axis == rhs.axis) && (lhs.side == rhs.side);
}

[[nodiscard]] auto get_base_coordinate(const erhe::math::Aabb& local_bounds, const Surface_base_frame& frame) -> float;
[[nodiscard]] auto get_top_coordinate (const erhe::math::Aabb& local_bounds, const Surface_base_frame& frame) -> float;

// The bounds whose base points the snap lines and the support rays pass through: the in-face sides move 1 mm (in
// world units, through the scale) inward, so a face flush with a side of the piece is not met along its edge.
[[nodiscard]] auto get_footprint_bounds(const erhe::math::Aabb& local_bounds, const glm::vec3 scale, const Surface_base_frame& frame) -> erhe::math::Aabb;

// The base face center, then its corners in (u, v) order: (min, min), (max, min), (min, max), (max, max).
[[nodiscard]] auto get_base_points(const erhe::math::Aabb& bounds, const Surface_base_frame& frame) -> std::array<glm::vec3, 5>;

[[nodiscard]] auto get_base_center(const erhe::math::Aabb& bounds, const Surface_base_frame& frame) -> glm::vec3;

// The point of the top face over a base point.
[[nodiscard]] auto get_top_point(const erhe::math::Aabb& local_bounds, const Surface_base_frame& frame, const glm::vec3 base_point) -> glm::vec3;

[[nodiscard]] inline auto get_world_point(const Surface_pose& pose, const glm::vec3 local_point) -> glm::vec3
{
    return pose.position + (pose.rotation * (pose.scale * local_point));
}

// Orient's rest: the start pose turned by the shortest rotation taking its up axis to the normal, about the base center,
// whose rest is `height` above it against the direction.
[[nodiscard]] auto get_oriented_pose(
    const Surface_pose&       start,
    const erhe::math::Aabb&   local_bounds,
    const Surface_base_frame& frame,
    const glm::vec3           direction,
    const float               height,
    const glm::vec3           normal
) -> Surface_pose;

[[nodiscard]] auto shortest_rotation(const glm::vec3 from, const glm::vec3 to) -> glm::quat;

} // namespace editor
