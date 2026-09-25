#pragma once

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
// and their bound-skeleton refusal (R9), shared by the Hierarchy context menu
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

}
