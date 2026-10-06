#pragma once

#include "assets/asset_reference.hpp"
#include "operations/operation.hpp"

#include "erhe_property/expression.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace erhe           { class Item_base; }
namespace erhe::property {
    class Dependency_property;
    class Property_set;
}

namespace editor {

class App_context;

// Undoable property edit recorded from an edit function
// (doc/editor/operations.md "Property_edit_operation"): the first execute
// runs the function once inside an erhe::property::Property_write_recording
// and keeps, per written (object, property), the local state before the
// first write and after the last. Redo applies the after states, undo the
// before states, both in record order except for the seal (an item's
// lock_edit record goes before its other records when it lifts the seal,
// after them when it seals), each through apply_item_property, so every
// write reaches App_context::on_item_property_changed; a state the item
// refuses is logged as an error.
//
// The edit function writes only through set_value / property setters;
// member writes that bypass the property layer (set_flag_bits,
// set_parent_from_node, a setter writing a plain member) are not recorded
// and are not undone.
//
// A write refused during the edit (a gate, a bridge validate, or an object
// value the D28 host check refuses), or an edit that wrote nothing, puts the
// operation in error: the writes that happened are restored and
// Operation_stack does not record it.
// What a Property_edit_operation adds to the recorded writes.
enum class Property_edit_follow_ups : unsigned int {
    // The recorded writes only.
    none         = 0,
    // After the first execute, the Node_transform_operations the connected
    // bone rule implies for every recorded Rig.tail / Rig.connected write on
    // an item (rig/bone_connect.hpp), as Property_set_operation records
    // them: redo runs them after the writes, undo undoes them (in reverse)
    // before the restores, so the edit and the moves are one undo step.
    bone_connect = 1
};

class Property_edit_operation : public Operation
{
public:
    using Edit_function = std::function<void()>;

    // One recorded property: the item that owns the written object and the
    // D29 sub-object index when the object is a mesh primitive.
    class Record
    {
    public:
        std::shared_ptr<erhe::Item_base>            item;
        std::optional<std::size_t>                  sub_object;
        const erhe::property::Dependency_property*  property{nullptr};
        std::optional<erhe::property::Local_state>  before;
        std::optional<erhe::property::Local_state>  after;
    };

    Property_edit_operation(std::string description, Edit_function edit, Property_edit_follow_ups follow_ups = Property_edit_follow_ups::none);
    ~Property_edit_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

    // The recorded writes, in record order; empty before the first execute
    // and when the operation is in error.
    [[nodiscard]] auto get_records() const -> std::span<const Record>;

private:
    enum class State : unsigned int {
        before = 0,
        after  = 1
    };

    void record (App_context& context);
    void apply  (App_context& context, State state);

    Edit_function                           m_edit;
    Property_edit_follow_ups                m_follow_up_kind{Property_edit_follow_ups::none};
    std::vector<Record>                     m_records;
    std::vector<std::shared_ptr<Operation>> m_follow_ups;
    // Object values (D28) naming managed assets in either state: this
    // operation is a declared user of each (Property_set_operation does
    // the same).
    std::vector<Asset_reference> m_userships;
};

// A bag of property values (D17) written as local values on one item, as
// one Property_edit_operation: the Properties window's Paste Properties.
// Each entry is checked against the item's live state just before it is
// written - the property's validation and the bridge validate
// (validate_value), the item's seal, and for an object value the D28 host
// check - and an entry the item refuses is skipped with a warning, so one
// refused entry (a copied name a sibling holds by then) does not fail the
// rest. Undo restores each written property's exact prior local layer
// (value, expression or none). Null when `values` is empty.
[[nodiscard]] auto make_property_set_edit_operation(
    App_context&                            context,
    const std::shared_ptr<erhe::Item_base>& item,
    const erhe::property::Property_set&     values
) -> std::shared_ptr<Property_edit_operation>;

}
