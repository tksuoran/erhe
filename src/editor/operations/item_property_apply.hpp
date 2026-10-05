#pragma once

#include "erhe_property/expression.hpp"

#include <cstddef>
#include <memory>
#include <optional>

namespace erhe           { class Item_base; }
namespace erhe::property {
    class Dependency_object;
    class Dependency_property;
}

namespace editor {

class App_context;

// The write step shared by Property_set_operation, Property_edit_operation
// and direct callers (doc/editor/operations.md "Property_edit_operation").
// Defined in item_property_apply.cpp, which needs no editor part beyond
// App_context::on_item_property_changed and is_item_reference_allowed;
// is_item_reference_allowed is defined in property_set_operation.cpp (it
// needs the scene registry and the asset manager).

// Applies `state` (a value, an expression, or nullopt = clear) as the
// item's local layer and runs the editor consequence hook. Shared by the
// property operations and by direct (non-undoable) callers such as the startup
// script. An object value (D28) is applied only when the referenced item
// belongs to the target's scene or is a cross-scene referenceable asset;
// otherwise a warning names both items and nothing changes. Returns whether
// the state was applied.
auto apply_item_property(
    App_context&                                       context,
    erhe::Item_base&                                   item,
    const erhe::property::Dependency_property&         property,
    const std::optional<erhe::property::Local_state>&  state
) -> bool;

// The same for a property of one of the item's sub-objects (D29):
// `target` is item.get_property_sub_object(index); a sealed item refuses
// the write, and the consequence hook runs with the item.
auto apply_item_property(
    App_context&                                       context,
    erhe::Item_base&                                   item,
    erhe::property::Dependency_object&                 target,
    const erhe::property::Dependency_property&         property,
    const std::optional<erhe::property::Local_state>&  state
) -> bool;

// The same with the sub-object named by index (D29): nullopt writes the
// item itself, an index writes item.get_property_sub_object(index); a
// sub-object that no longer exists is logged and nothing changes. The
// apply step of Property_set_operation and Property_edit_operation.
auto apply_item_property(
    App_context&                                       context,
    erhe::Item_base&                                   item,
    const std::optional<std::size_t>&                  sub_object,
    const erhe::property::Dependency_property&         property,
    const std::optional<erhe::property::Local_state>&  state
) -> bool;

// The item an object value (D28) names, or null (not an object value, a
// null reference, an expression, no local state).
[[nodiscard]] auto get_referenced_item(const std::optional<erhe::property::Local_state>& state) -> std::shared_ptr<erhe::Item_base>;

// The D28 host check apply_item_property makes before an object value is
// written on `target`: same scene, or a manager-owned asset that is
// cross-scene referenceable; an item whose scene cannot be determined
// passes. A refusal is logged with both items.
[[nodiscard]] auto is_item_reference_allowed(App_context& context, const erhe::Item_base& target, const erhe::Item_base& referenced) -> bool;


// D24: a sub-object of a sealed item is as sealed as the item, although
// the sub-object (a mesh primitive) carries no seal of its own. True when
// `target` is a sub-object of `item` and `item` is sealed.
[[nodiscard]] auto is_sealed_sub_object(const erhe::Item_base& item, const erhe::property::Dependency_object& target) -> bool;

}
