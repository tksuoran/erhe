#pragma once

#include "erhe_scene/trs_transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace editor {

// The mirror math of Symmetrize (doc/plans/rigging/skeleton_editing.md R13).
// Pure functions of glm values; rig/bone_structure.cpp reads the scene and
// applies them.
//
// S = diag(-1, 1, 1) is the reflection across an X = 0 plane. The mirror
// plane of a skeleton is the X = 0 plane of its SKELETON FRAME: the frame of
// the skeleton root's parent node, in which the root's own local transform
// and rest sit (the armature space of Blender's Symmetrize; world when the
// root has no parent). The mirror of a bone b is the bone whose frame is
// S_w * W(b) * S, where W(b) is b's world transform and
// S_w = F * S * inverse(F) the reflection across the plane in world (F the
// skeleton frame's world transform): a point with local coordinates
// (x, y, z) in b maps to the point with local coordinates (-x, y, z) in the
// mirror bone, so the mirror bone's frame is proper (det +1) and its local X
// axis is the mirror image of b's negated.

// S = diag(-1, 1, 1) as a matrix.
[[nodiscard]] auto get_mirror_x_matrix() -> glm::mat4;

// A local vector of b in the mirror bone's frame: (x, y, z) -> (-x, y, z).
// Rig.tail of the mirror bone.
[[nodiscard]] auto mirror_vector_x(const glm::vec3& v) -> glm::vec3;

// S * R(q) * S as a quaternion: (w, x, y, z) -> (w, x, -y, -z). A rotation
// about X keeps its angle; rotations about Y and Z change sign.
[[nodiscard]] auto mirror_rotation_x(const glm::quat& q) -> glm::quat;

// S * T * S in TRS form, exactly (no matrix decomposition): translation x
// negated, rotation by mirror_rotation_x, scale unchanged. The local
// transform (and rest) of the mirror bone when its parent is the mirror of
// b's parent, and when b is a skeleton root (its parent frame is the
// skeleton frame, the mirror plane's own frame).
[[nodiscard]] auto mirror_trs_x(const erhe::scene::Trs_transform& transform) -> erhe::scene::Trs_transform;

// The general case: the mirror bone's local transform G * local * S, where
// G maps b's parent frame to the mirror bone's parent frame through the
// mirror (det -1):
//   pose: G = inverse(W(new parent)) * S_w * W(b's parent)
//   rest: G = inverse(Rs(new parent)) * S * Rs(b's parent), Rs(n) the rest
//         transform of n relative to the skeleton frame (the product of the
//         Rig.rest_* local transforms from the skeleton root down to n;
//         identity for the skeleton frame's own node).
// With G = S it equals mirror_trs_x (up to rounding).
[[nodiscard]] auto mirror_local_transform(const glm::mat4& parent_map, const glm::mat4& local) -> erhe::scene::Trs_transform;

// S_w = frame * S * inverse(frame): the reflection across the X = 0 plane of
// the frame whose world transform is `frame`.
[[nodiscard]] auto get_mirror_plane_matrix(const glm::mat4& frame) -> glm::mat4;

// Ik.limit_min / Ik.limit_max of the mirror bone. The limited quantity is
// the rotation inverse(Ik.rest_rotation) * local_rotation, which the mirror
// conjugates by S (mirror_rotation_x): an angle about X is kept, angles about
// Y and Z change sign. So the X range is copied and the Y and Z ranges are
// negated and swapped: [min, max] -> [-max, -min]. The coerced ranges
// (min in [-pi, 0], max in [0, pi]) map onto themselves. Locks, limit
// flags and stiffness are per-axis magnitudes and are copied unchanged.
class Ik_limit_range
{
public:
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};
[[nodiscard]] auto mirror_ik_limits(const glm::vec3& limit_min, const glm::vec3& limit_max) -> Ik_limit_range;

// Ik.pole_angle of the mirror bone: a swivel angle about the chain's
// root-to-effector line, which the reflection reverses: -angle.
[[nodiscard]] auto mirror_pole_angle(float angle) -> float;

}
