#pragma once

#include <memory>
#include <optional>
#include <string>

namespace erhe {
    class Hierarchy;
    class Item_base;
}

namespace editor {

class Prefab_instance;

// What a reference arc protects is STRUCTURE, not values
// (doc/usd-compatibility-plan.md X2): under a prim carrying a
// Prefab_instance attachment no prim is added, removed or reparented, while
// every property of every item inside the instance stays editable (a local
// value there is an override of the reference layer).
//
// The two predicates answer that question for the two shapes a structural
// edit takes; both return the user-facing reason when the edit is refused,
// and nothing when it is allowed. Each walks the item's ancestors.
//
// Building the message costs a formatted string, so the two bool forms
// below answer the same question without one. Per-frame UI code - the item
// tree asks on every row of every frame whether a drop is offered - uses
// those; the edit and popup paths, which need the reason to log or to show,
// use the message forms.

// True (a reason) when `item` is INSIDE an instance - it or an ancestor of
// it hangs below a prim carrying a Prefab_instance attachment - so the item
// cannot be removed or reparented. The carrier itself is a normal scene
// prim: deleting or moving the whole instance is allowed.
[[nodiscard]] auto instance_structure_refusal(const erhe::Item_base& item) -> std::optional<std::string>;

// True (a reason) when nothing can be added under `parent`: `parent` is an
// instance carrier, or is itself inside an instance.
[[nodiscard]] auto instance_child_refusal(const erhe::Hierarchy& parent) -> std::optional<std::string>;

// The same two questions, answered without formatting a message.
[[nodiscard]] auto is_instance_structure_protected(const erhe::Item_base& item) -> bool;
[[nodiscard]] auto refuses_instance_child(const erhe::Hierarchy& parent) -> bool;

// The Hierarchy an item's position in the tree is that of: the item itself,
// or - for a Node_attachment, whose position is its prim's - the prim it is
// attached to. nullptr for an item that is in no tree.
[[nodiscard]] auto get_structural_hierarchy(const erhe::Item_base& item) -> const erhe::Hierarchy*;

// Where an item sits inside a prefab instance
// (doc/usd-compatibility-plan.md X1, X5). `carrier` is the referencing prim
// at or above the item and `prefab_instance` its first arc; `relative_path`
// is the item's M1 path below the arc's target clone, which is the carrier's
// own child - so an empty path means the item IS that clone, the level a USD
// save collapses onto the carrier prim. Everything is null / empty when the
// item is inside no instance.
class Instance_position final
{
public:
    const erhe::Hierarchy*           carrier{nullptr};
    std::shared_ptr<Prefab_instance> prefab_instance{};
    std::string                      relative_path{};
};

[[nodiscard]] auto find_instance_position(const erhe::Item_base& item) -> Instance_position;

// Whether this instance's interior is sealed (lock_edit and the viewport
// locks, seal_instance_subtree): the glTF prefab editing model of
// doc/gltf-prefabs-plan.md. A USD-backed instance is never sealed - USD
// seals no property - and is protected by the two predicates above alone.
[[nodiscard]] auto is_sealed_prefab_instance(const Prefab_instance& prefab_instance) -> bool;

} // namespace editor
