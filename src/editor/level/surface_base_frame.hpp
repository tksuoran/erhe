#pragma once

#include "level/surface_snap_search.hpp"

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

// Snap without orient (U51): of the six bounds faces the one whose outward normal, taken to world space by the rotation
// and the scale's signs, is most along the direction; an exact tie keeps -Y (-Y, +Y, -X, +X, -Z, +Z).
[[nodiscard]] auto get_surface_base_frame(
    const glm::quat& rotation,
    const glm::vec3& scale,
    const glm::vec3& direction
) -> Surface_base_frame;

// Orient (U51): the local -Y face, whichever way the pose turns it; orient turns it onto the surface.
[[nodiscard]] auto get_orient_base_frame(const glm::vec3& scale) -> Surface_base_frame;

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

// Orient's rest, turned about the base center, whose rest is `height` above it against the direction: the shortest rotation
// taking the up axis to the normal, after a half turn about the face's u axis when the up axis points along the direction.
[[nodiscard]] auto get_oriented_pose(
    const Surface_pose&       start,
    const erhe::math::Aabb&   local_bounds,
    const Surface_base_frame& frame,
    const glm::vec3           direction,
    const float               height,
    const glm::vec3           normal
) -> Surface_pose;

[[nodiscard]] auto shortest_rotation(const glm::vec3 from, const glm::vec3 to) -> glm::quat;

class Anchor_rest
{
public:
    bool         found   {false};
    Surface_pose pose;           // the start pose itself when nothing was found or the start already rests
    float        distance{0.0f}; // how far the origin moved along the direction; negative when it rose
};

// U60 rest anchor without orient: the start pose's origin moved along the direction onto the line's nearest surface,
// rotation and scale kept; a start within the tolerance of its rest is returned bit for bit.
[[nodiscard]] auto get_anchor_rest(
    const Surface_pose&         start,
    const glm::vec3             direction,
    const Surface_line&         line,
    const Surface_search_window window
) -> Anchor_rest;

enum class Orient_half_turn : unsigned int {
    off = 0,
    on      // the up axis points along the search direction (a ceiling): half a turn about the face's u axis first
};

[[nodiscard]] auto get_orient_half_turn(const glm::quat& rotation, const Surface_base_frame& frame, const glm::vec3 direction) -> Orient_half_turn;

// Orient's turn in world space, applied before the start rotation: the shortest rotation taking the frame's up axis to the
// normal, after the half turn when it is on.
[[nodiscard]] auto get_orient_turn(
    const glm::quat&          rotation,
    const Surface_base_frame& frame,
    const Orient_half_turn    half_turn,
    const glm::vec3           normal
) -> glm::quat;

// Orient's one line (U51): through the -Y face center, the pivot of the turn.
class Orient_search
{
public:
    Surface_base_frame    frame;            // the -Y face
    glm::vec3             base_center{0.0f}; // world
    float                 span       {0.0f}; // the whole base-to-top extent: the turned piece stands along the normal
    Surface_search_window window;            // shifted so the distances are measured from the Y face leading along the direction
};

[[nodiscard]] auto get_orient_search(
    const erhe::math::Aabb&     local_bounds,
    const Surface_pose&         start,
    const glm::vec3             direction,
    const Surface_search_window window
) -> Orient_search;

} // namespace editor
