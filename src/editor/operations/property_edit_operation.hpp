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
namespace erhe::property { class Dependency_property; }

namespace editor {

class App_context;

// Undoable property edit recorded from an edit function
// (doc/editor/operations.md "Property_edit_operation"): the first execute
// runs the function once inside an erhe::property::Property_write_recording
// and keeps, per written (object, property), the local state before the
// first write and after the last. Redo applies the after states, undo the
// before states, both in record order, each through apply_item_property, so
// every write reaches App_context::on_item_property_changed.
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

    Property_edit_operation(std::string description, Edit_function edit);
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

    Edit_function                m_edit;
    std::vector<Record>          m_records;
    // Object values (D28) naming managed assets in either state: this
    // operation is a declared user of each (Property_set_operation does
    // the same).
    std::vector<Asset_reference> m_userships;
};

}
