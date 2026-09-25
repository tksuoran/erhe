#pragma once

#include <glm/glm.hpp>

#include <cstddef>

namespace erhe::scene {
    class Skin;
    class Xformable; using Node = Xformable;
}

namespace editor {

// A bone's tail in its own LOCAL space (the head is the node origin): the
// computed default of Rig.tail (doc/plans/rigging/skeleton_editing.md R3).
// Local rather than world on purpose: the value depends only on child local
// translations, so a rotation-only animation - the common case - never changes
// it, and a proxy parented under the bone needs no per-frame refresh.
//
// Rule, for a node a skin lists (the first skin in Scene::get_skins() order,
// erhe::scene::find_skin_joint): infer_skinned_bone_tail. For any other node:
//   1. A node with bone children (erhe::scene::is_bone): the first bone
//      child's local translation (the single child's head for a chain; the
//      first when several disagree, as for a skinned joint without bounds).
//   2. A leaf under a bone parent: local +Y, as long as the parent's tail
//      (Rig.tail) - a chain grown by hand keeps its bone length at the end.
//   3. A leaf with no bone parent: (0, 1, 0), one scene unit along the bone
//      axis +Y, the axis a bone created in the editor points along (R4).
[[nodiscard]] auto compute_default_bone_tail(const erhe::scene::Node& node) -> glm::vec3;

// The tail of joint `joint_index` of `skin`, from the skin's joints and the
// skinned vertices:
//   1. Child joints that agree on a location (a single child, or several with
//      the same local translation): that translation.
//   2. Leaf joints and joints whose children disagree (a hand fanning into
//      fingers): the direction still follows the hierarchy (the first child's
//      direction when there were children, local +Y for a leaf), and the
//      rest-pose bounds of the vertices the joint skins
//      (Buffer_mesh::joint_bounding_boxes transformed into joint space)
//      supply only the LENGTH - the farthest box corner's projection onto
//      that direction.
//   3. No skinned bounds: the first child's translation if there were
//      (disagreeing) children; else, for a joint with a parent, the parent
//      offset length along local +Y; else a short local +X stub.
[[nodiscard]] auto infer_skinned_bone_tail(const erhe::scene::Skin& skin, std::size_t joint_index) -> glm::vec3;

} // namespace editor
