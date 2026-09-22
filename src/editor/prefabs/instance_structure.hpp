#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>

namespace erhe {
    class Composition_arc;
    class Hierarchy;
    class Item_base;
}
namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace editor {

// What a reference arc protects is STRUCTURE, not values
// (doc/erhe/usd_compatibility_design.md X2): under a prim carrying a composition
// arc (doc/erhe/item.md "Composition arcs") no prim is added, removed or
// reparented, while every property of every item inside the instance stays
// editable (a local value there is an override of the reference layer).
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
// it hangs below a prim carrying a composition arc - so the item cannot be
// removed or reparented. The carrier itself is a normal scene prim:
// deleting or moving the whole instance is allowed.
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

// The arcs `item` carries, empty for every item that carries none and for an
// item that is no prim.
[[nodiscard]] auto get_instance_arcs(const erhe::Item_base& item) -> std::span<const erhe::Composition_arc>;

// The first arc of `item`, null when it carries none. The first arc is the
// one that answers for the prim where one arc has to stand for the carrier:
// the seal, the provenance of a value read through the reference layer and
// the "Load '<source>'" entry.
[[nodiscard]] auto get_first_instance_arc(const erhe::Item_base& item) -> const erhe::Composition_arc*;

// True when `item` carries at least one composition arc.
[[nodiscard]] auto is_instance_carrier(const erhe::Item_base& item) -> bool;

// Where an item sits inside a prefab instance
// (doc/erhe/usd_compatibility_design.md X1, X5). `carrier` is the referencing prim
// at or above the item and `arc` its first arc; `relative_path`
// is the item's M1 path below the arc's target clone, which is the carrier's
// own child - so an empty path means the item IS that clone, the level a USD
// save collapses onto the carrier prim. Everything is null / empty when the
// item is inside no instance. `arc` points into the carrier's own arc list,
// so it is valid exactly as long as `carrier` is.
class Instance_position final
{
public:
    const erhe::Hierarchy*       carrier{nullptr};
    const erhe::Composition_arc* arc{nullptr};
    std::string                  relative_path{};
};

[[nodiscard]] auto find_instance_position(const erhe::Item_base& item) -> Instance_position;

// Whether an instance carried by this arc has a sealed interior (lock_edit
// and the viewport locks, seal_instance_subtree): the glTF prefab editing
// model of doc/plans/gltf_prefabs.md. A USD-backed instance is never sealed -
// USD seals no property - and is protected by the two predicates above alone.
[[nodiscard]] auto is_sealed_prefab_instance(const erhe::Composition_arc& arc) -> bool;

// The same question asked of a prim: its first arc decides, and a prim that
// carries no arc is no instance root.
[[nodiscard]] auto is_sealed_instance_carrier(const erhe::Item_base& item) -> bool;

// Returns the outermost node, walking up from and including the given node,
// that carries a SEALED composition arc (a glTF template,
// is_sealed_prefab_instance); nullptr when the node is inside no sealed
// instance. A sealed instance subtree is not editable in the containing
// scene, so picking anything inside one resolves to the instance root, and
// nested sealed instances resolve to the outermost one. A USD-backed
// instance is not sealed: its interior picks and selects like any other prim
// (doc/erhe/usd_compatibility_design.md X2).
[[nodiscard]] auto get_outermost_prefab_instance_node(erhe::scene::Node* node) -> erhe::scene::Node*;

} // namespace editor
