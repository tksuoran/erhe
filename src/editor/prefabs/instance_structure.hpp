#pragma once

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
// and nothing when it is allowed. Each walks the item's ancestors, which is
// change-driven work: an edit, never a frame.

// True (a reason) when `item` is INSIDE an instance - it or an ancestor of
// it hangs below a prim carrying a Prefab_instance attachment - so the item
// cannot be removed or reparented. The carrier itself is a normal scene
// prim: deleting or moving the whole instance is allowed.
[[nodiscard]] auto instance_structure_refusal(const erhe::Item_base& item) -> std::optional<std::string>;

// True (a reason) when nothing can be added under `parent`: `parent` is an
// instance carrier, or is itself inside an instance.
[[nodiscard]] auto instance_child_refusal(const erhe::Hierarchy& parent) -> std::optional<std::string>;

// Whether this instance's interior is sealed (lock_edit and the viewport
// locks, seal_instance_subtree): the glTF prefab editing model of
// doc/gltf-prefabs-plan.md. A USD-backed instance is never sealed - USD
// seals no property - and is protected by the two predicates above alone.
[[nodiscard]] auto is_sealed_prefab_instance(const Prefab_instance& prefab_instance) -> bool;

} // namespace editor
