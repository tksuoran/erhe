#pragma once

#include "rig/bone_roll.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace editor {

class App_context;

// The bone creation and structure verbs of doc/plans/rigging/skeleton_editing.md
// slice C (R5 Create Bone, R6 Extrude, R7 Subdivide, R8 Delete / Dissolve)
// and their bound-skeleton refusal (R9), R13 Symmetrize and R16 Recalculate
// Roll / Align to Active, shared by the Hierarchy context menu
// and the MCP tools. Each verb queues ONE Compound_operation (executed on the
// next Operation_stack::update) whose undo restores names, flags, Rig values
// and local transforms exactly. One-shot commands: nothing here runs per
// frame.

// R9: the refusal of a structure edit of `bone` when a skin lists it - the
// message names the bone and the skin (skin inverse binds and weights would
// go stale; editing a bound skeleton is Phase 6). nullopt when no skin lists
// the node.
[[nodiscard]] auto get_bound_bone_refusal(const erhe::scene::Node& bone) -> std::optional<std::string>;

// The first refusal among `bones` (get_bound_bone_refusal), nullopt when
// none of them is bound. For greying out menu entries.
[[nodiscard]] auto get_bound_bones_refusal(const std::vector<std::shared_ptr<erhe::scene::Node>>& bones) -> std::optional<std::string>;

class Bone_structure_result
{
public:
    // The bones the verb creates, in creation order (Create: one; Extrude:
    // one per target; Subdivide: the N - 1 new pieces per target).
    std::vector<std::shared_ptr<erhe::scene::Node>> created;
    // The bones the verb removes (Delete / Dissolve).
    std::vector<std::shared_ptr<erhe::scene::Node>> removed;
    // The bones whose frame the verb turns (Recalculate Roll, Align to
    // Active).
    std::vector<std::shared_ptr<erhe::scene::Node>> changed;
    // Set when the verb was refused (R9 or an invalid argument); the text is
    // the message that was logged. Nothing is queued then.
    std::optional<std::string>                      refusal;
    // True when one undoable operation was queued.
    bool                                            queued{false};
};

// R5 Create Bone: a new bone node, last child of `parent` (a bone, any other
// node, or the scene's root node). There is no 3D cursor in the editor, so
// the head is the parent's tail (Rig.tail) when the parent is a bone and the
// parent's origin otherwise (the scene origin under the root node); the local
// rotation is identity and the tail is one scene unit along the parent's
// bone axis (the direction of the parent's Rig.tail), +Y under a non-bone.
// The new bone carries the bone flag, a local Rig.tail and its creation local
// TRS as local Rig.rest_translation / rest_rotation / rest_scale values; it
// is not connected. It becomes the selection and the active item. `name`
// empty means "Bone"; the name is made unique among the parent's children
// and the parent's skeleton ("Bone.001", ...). Refused (R9) when the parent
// is a bone a skin lists.
auto create_bone(
    App_context&                              context,
    const std::shared_ptr<erhe::scene::Node>& parent,
    std::string_view                          name
) -> Bone_structure_result;

// R6 Extrude: for each target bone a new connected child bone whose head is
// the target's tail, local rotation identity, tail equal to the target's
// tail (same direction and length) and rest values recorded as for Create
// Bone. Named `<base>.NNN`, the first free index in the skeleton (`base` is
// the target's name without a trailing `.NNN`). The new bones become the
// selection (the first one active), so a repeated Extrude grows a chain.
auto extrude_bones(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets
) -> Bone_structure_result;

// R7 Subdivide: each target bone becomes `count` (>= 2) connected bones
// along its head-to-tail segment. The target keeps its name, head and rest
// and its tail becomes tail / count; `count - 1` new bones follow as a chain
// of children, each translated by tail / count with identity rotation, named
// `<base>.NNN` as for Extrude, rest values recorded. The target's other
// children re-parent to the last piece keeping their world transforms (a
// connected child's head lands exactly on the last piece's tail); an unbound
// bone child's Rig.rest_translation moves by the same offset so its rest
// stays where it was. The target and its pieces become the selection.
auto subdivide_bones(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets,
    std::size_t                                            count
) -> Bone_structure_result;

enum class Bone_delete_mode : unsigned int {
    delete_bones = 0, // R8 Delete
    dissolve     = 1  // R8 Dissolve
};

// R8 Delete / Dissolve: removes the target bones. Their children re-parent to
// the removed bone's parent keeping world transforms; an unbound bone child's
// rest becomes rest(removed) * rest(child) so its rest stays where it was.
// Dissolve additionally extends the parent's tail (Rig.tail) to the removed
// bone's tail when the parent is a bone and the removed bone was its only
// connected bone child; the removed bone's connected children then stay
// connected to the parent (their heads are on its new tail). Every other
// connected child of a removed bone is disconnected (Rig.connected cleared),
// as its head is no longer on its new parent's tail. The selection is
// cleared.
auto delete_bones(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets,
    Bone_delete_mode                                       mode
) -> Bone_structure_result;

// The menu / tool label of a delete mode ("Delete Bone" / "Dissolve Bone").
[[nodiscard]] auto get_bone_delete_mode_label(Bone_delete_mode mode) -> const char*;

// R13 Symmetrize: for each target bone whose name has a side
// (rig/bone_naming.hpp) and that has no mirror counterpart (R12
// find_mirror_bone; for a skeleton root, no sibling of the flipped name),
// creates the mirror bone across the X = 0 plane of the skeleton frame - the
// frame of the skeleton root's parent node (rig/bone_mirror.hpp). Targets are
// processed parents first, so a whole selected side is mirrored in one step.
// The mirror bone is named flip_side_name(name) (made unique as for Create
// Bone when another node holds that name) and is the last child of: the
// mirror bone this verb creates for the target's parent, else the parent's
// counterpart, else the parent itself (a parent without a side or without a
// counterpart; a skeleton root's parent). It carries the bone flag and:
// - local transform: the mirror of the target's current world transform,
//   expressed under the new parent (mirror_trs_x when the new parent is the
//   mirror of the target's parent or the target is a skeleton root,
//   mirror_local_transform otherwise);
// - Rig.tail: mirror_vector_x(tail); Rig.rest_*: the mirror of the target's
//   rest in the skeleton frame, expressed under the new parent's rest;
// - Rig.connected: copied (cleared, with a logged note, when the mirrored
//   head is not on the new parent's tail);
// - the target's local Ik.* values: locks, limit flags and stiffness copied,
//   limit_min / limit_max by mirror_ik_limits, rest_rotation mirrored like
//   the rest, pole_angle negated, pole_target mapped to the pole's mirror
//   (the mirror bone this verb creates for it, the pole bone's counterpart,
//   or the pole's sibling of the flipped name; not copied otherwise).
// Targets without a side or with a counterpart are skipped (logged). One
// Compound_operation; the new bones become the selection. Refused (R9) when
// a target, or an existing bone a mirror bone would be created under, is a
// bone a skin lists.
auto symmetrize_bones(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets
) -> Bone_structure_result;

// What Recalculate Roll and Align to Active change, as ONE
// Compound_operation, refused (R9) when a target is a bone a skin lists:
// each target's frame turns about its head by its own change C (local
// L -> L * C, rig/bone_roll.hpp apply_frame_change). Its children keep their
// world transforms (local -> inverse(C) * local), except that a connected
// child bone keeps its head on the target's tail (its local translation and
// its world rotation are kept). A target whose parent is also a target
// combines both. The rest transform (Rig.rest_translation / rest_rotation)
// of every changed unbound bone gets the same change, so the pose relative
// to rest is unchanged, and a local Ik.rest_rotation (the IK limits frame)
// turns with it. Undo restores the local transforms and values exactly.

// R16 Recalculate Roll: turns each target bone about its head-to-tail axis
// (compute_roll_angle) so its local `axis` points as close as possible to
// the world direction `reference`. Rig.tail is on the rotation axis, so the
// tail point stays put. Targets whose axis or reference is parallel to the
// bone axis are skipped (logged); refused when that leaves none.
auto recalculate_bone_roll(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets,
    Roll_axis                                              axis,
    const glm::vec3&                                       reference
) -> Bone_structure_result;

// R16 Align to Active: gives each target bone (the active bone itself
// excluded) the active bone's head-to-tail direction and roll
// (compute_align_change): the head stays, the tail keeps its length and turns
// onto the active bone's direction. A target's Rig.tail without a local value
// is recorded as one (its current value), so a default that follows a
// child's head does not turn it back.
auto align_bones_to_active(
    App_context&                                           context,
    const std::vector<std::shared_ptr<erhe::scene::Node>>& targets,
    const std::shared_ptr<erhe::scene::Node>&              active
) -> Bone_structure_result;

// The label of a roll axis ("X" / "Z").
[[nodiscard]] auto get_roll_axis_label(Roll_axis axis) -> const char*;

}
