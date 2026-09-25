#pragma once

#include "erhe_scene/trs_transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>

namespace editor {

// The bone orientation math of doc/plans/rigging/skeleton_editing.md R16:
// Recalculate Roll and Align to Active. Pure functions of glm values;
// rig/bone_structure.cpp reads the scene and applies them.
//
// Both verbs turn a bone's frame about its head by a rotation C expressed in
// the bone's own local frame (the "own change"): its local transform L
// becomes L * C, so the head stays and the world rotation becomes O * C.
// Rig.tail is a vector of the bone's frame, so it turns with the frame: a
// roll's C is about the tail's own direction, which it leaves in place, so
// the tail point stays put in world; Align's C turns the tail onto the
// active bone's direction.

// The bone-local axis a roll aims.
enum class Roll_axis : unsigned int {
    x = 0,
    z = 1
};

[[nodiscard]] auto get_roll_axis_vector(Roll_axis axis) -> glm::vec3;

// Recalculate Roll: the angle, about the bone's head-to-tail axis, that
// turns the local axis `local_axis` as close as possible to the world
// direction `reference`: the signed angle from the axis's world direction
// to the reference, both projected onto the plane perpendicular to the
// bone's world head-to-tail axis. `world_rotation` is the bone's world
// rotation, `local_tail` its Rig.tail. nullopt when the tail is zero or the
// reference or the axis is parallel to the bone axis (no roll aims it).
[[nodiscard]] auto compute_roll_angle(
    const glm::quat& world_rotation,
    const glm::vec3& local_tail,
    const glm::vec3& local_axis,
    const glm::vec3& reference
) -> std::optional<float>;

// The own change of a roll by `angle`: the rotation by `angle` about the
// bone-local direction of `local_tail`. nullopt for a zero tail.
[[nodiscard]] auto make_roll_change(const glm::vec3& local_tail, float angle) -> std::optional<glm::quat>;

// Align to Active: the own change that gives the bone the active bone's
// head-to-tail direction and roll. The new world rotation is
// active_world_rotation * Q, Q the shortest rotation taking the direction
// of `local_tail` onto the direction of `active_local_tail` (identity when
// both bones use the same local bone axis, e.g. +Y: the bone then takes the
// active bone's world rotation), so the bone's tail direction in world is
// the active bone's. nullopt for a zero tail on either bone.
[[nodiscard]] auto compute_align_change(
    const glm::quat& world_rotation,
    const glm::vec3& local_tail,
    const glm::quat& active_world_rotation,
    const glm::vec3& active_local_tail
) -> std::optional<glm::quat>;

// How a node's head follows its parent's frame change.
enum class Frame_change_head : unsigned int {
    // The node keeps its world position: its local translation turns by the
    // inverse of the parent's change.
    keep_world       = 0,
    // A connected bone: its head stays on the parent's tail, a vector of the
    // parent's frame, so its local translation is kept.
    on_parent_tail   = 1
};

// The local transform (or rest transform) of a node after its parent's frame
// turns by `parent_change` and its own frame by `own_change` (both about the
// respective origin, each in its own frame):
//   translation: inverse(parent_change) * t (keep_world) or t (on_parent_tail)
//   rotation:    inverse(parent_change) * r * own_change
//   scale:       unchanged
// The node's world rotation becomes O * own_change; with an identity own
// change and keep_world its world transform is unchanged.
[[nodiscard]] auto apply_frame_change(
    const erhe::scene::Trs_transform& local,
    const glm::quat&                  parent_change,
    const glm::quat&                  own_change,
    Frame_change_head                 head
) -> erhe::scene::Trs_transform;

}
