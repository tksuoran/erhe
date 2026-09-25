#pragma once

#include <memory>
#include <span>
#include <vector>

namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace editor {

// Skeleton walks for the selection helpers and the side naming verbs
// (doc/plans/rigging/skeleton_editing.md R10, R12). A bone is a node for
// which erhe::scene::is_bone() holds. Pure scene-graph queries: nothing here
// changes the scene or the selection.

enum class Bone_select_mode : unsigned int {
    parent             = 0, // the bone parent of each target
    children           = 1, // the immediate bone children of each target
    children_recursive = 2, // every bone below each target
    chain              = 3, // the chain each target is on (collect_bone_chain)
    mirror             = 4  // the mirror counterpart of each target (find_mirror_bone)
};

// The skeleton root of a bone: the topmost bone reached by walking bone
// parents up from it, i.e. the first ancestor-or-self bone whose parent is
// not a bone. Two bones are in the same skeleton when they have the same
// skeleton root. Null when `bone` is not a bone.
[[nodiscard]] auto get_skeleton_root(const std::shared_ptr<erhe::scene::Node>& bone) -> std::shared_ptr<erhe::scene::Node>;

// The bone children of a node, in child order (non-bone children skipped).
void collect_bone_children(const erhe::scene::Node& node, std::vector<std::shared_ptr<erhe::scene::Node>>& out);

// The chain `bone` is on, from its top to its bottom. A chain is a maximal
// run of bones in which each link's parent has exactly one bone child: the
// walk goes up while the bone parent has exactly one bone child (a branching
// parent ends the chain above it), and down while the current bone has
// exactly one bone child (a branching bone is the last bone of its chain).
// Appends to `out`; nothing when `bone` is not a bone.
void collect_bone_chain(const std::shared_ptr<erhe::scene::Node>& bone, std::vector<std::shared_ptr<erhe::scene::Node>>& out);

// The mirror counterpart of a bone (R12): the first bone, in pre-order from
// the skeleton root, whose name is flip_side_name(bone's name). Null when the
// name has no side, no such bone exists in the skeleton, or `bone` is not a
// bone.
[[nodiscard]] auto find_mirror_bone(const std::shared_ptr<erhe::scene::Node>& bone) -> std::shared_ptr<erhe::scene::Node>;

// The bones `mode` selects from `targets`, deduplicated, in target order
// (for each target, its results in hierarchy order). Non-bone targets
// contribute nothing. `out` is cleared first.
void collect_bone_selection(
    std::span<const std::shared_ptr<erhe::scene::Node>> targets,
    Bone_select_mode                                    mode,
    std::vector<std::shared_ptr<erhe::scene::Node>>&    out
);

}
