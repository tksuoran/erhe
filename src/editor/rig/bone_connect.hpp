#pragma once

#include <memory>
#include <vector>

namespace erhe          { class Item_base; }
namespace erhe::property { class Dependency_property; }

namespace editor {

class Operation;

// Connected bones (doc/plans/rigging/skeleton_editing.md R4): a bone with
// Rig.connected keeps its head (its local translation) on its parent's
// Rig.tail. After an edit of `property` on `item` has been applied, appends
// the transform operations that restore the rule:
//   - Rig.tail on a node: one Node_transform_operation per connected bone
//     child whose translation is not already the new tail;
//   - Rig.connected on a node, now true: one for the node itself, onto its
//     parent's tail.
// Other properties append nothing. Property_set_operation runs these after
// its own write and undoes them before it, so the edit and the moves are one
// undo step.
void append_bone_connect_follow_ups(
    const erhe::Item_base&                     item,
    const erhe::property::Dependency_property& property,
    std::vector<std::shared_ptr<Operation>>&   out_operations
);

} // namespace editor
