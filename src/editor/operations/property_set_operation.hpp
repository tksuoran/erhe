#pragma once

#include "assets/asset_reference.hpp"
#include "operations/operation.hpp"

#include "erhe_property/expression.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_value.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe          { class Item_base; }
namespace erhe::property { class Dependency_property; }

namespace editor {

class App_context;

// Undoable write of one property on one item (doc/erhe/property_system.md
// D11). `before` / `after` are the item's LOCAL state - a stored value or an
// expression (D22), nullopt meaning "no local value" - so undo restores a
// cleared property or the formula a value replaced, not merely the previous
// effective value. After each apply the operation runs
// App_context::on_item_property_changed so the property's consequence flags
// take effect. With a sub-object index (D29) the target is
// item->get_property_sub_object(index) - a mesh primitive - and the item
// stays the one the operation names and seals against.
class Property_set_operation : public Operation
{
public:
    Property_set_operation(
        const std::shared_ptr<erhe::Item_base>&      item,
        const erhe::property::Dependency_property&   property,
        std::optional<erhe::property::Local_state>   before,
        std::optional<erhe::property::Local_state>   after
    );
    Property_set_operation(
        const std::shared_ptr<erhe::Item_base>&      item,
        std::optional<std::size_t>                   sub_object,
        const erhe::property::Dependency_property&   property,
        std::optional<erhe::property::Local_state>   before,
        std::optional<erhe::property::Local_state>   after
    );
    Property_set_operation(
        const std::shared_ptr<erhe::Item_base>&       item,
        const erhe::property::Dependency_property&    property,
        std::optional<erhe::property::Property_value> before,
        std::optional<erhe::property::Property_value> after
    );
    ~Property_set_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

    [[nodiscard]] auto get_item      () const -> const std::shared_ptr<erhe::Item_base>&    { return m_item; }
    [[nodiscard]] auto get_sub_object() const -> const std::optional<std::size_t>&           { return m_sub_object; }
    [[nodiscard]] auto get_property  () const -> const erhe::property::Dependency_property& { return m_property; }

private:
    void apply(App_context& context, const std::optional<erhe::property::Local_state>& state);
    void adopt_userships(App_context& context);

    std::shared_ptr<erhe::Item_base>            m_item;
    std::optional<std::size_t>                  m_sub_object;
    const erhe::property::Dependency_property&  m_property;
    std::optional<erhe::property::Local_state>  m_before;
    std::optional<erhe::property::Local_state>  m_after;
    // An object property (D28) naming a managed asset: while recorded, this
    // operation is a declared user of the asset in either state (undo puts
    // the other one back). Adopted at first execute, as
    // Mesh_material_assign_operation does.
    std::vector<Asset_reference>                m_userships;
    bool                                        m_userships_adopted{false};
};

// Applies a Property_set to several items (paste, multi-selection edit) as
// one undo step: every entry becomes a local value on every item, and undo
// restores each item's previous local value (or clears it).
class Property_set_apply_operation : public Operation
{
public:
    Property_set_apply_operation(
        const std::vector<std::shared_ptr<erhe::Item_base>>& items,
        erhe::property::Property_set                         values
    );
    ~Property_set_apply_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

private:
    struct Target
    {
        std::shared_ptr<erhe::Item_base>                           item;
        std::vector<std::optional<erhe::property::Property_value>> before; // one per m_values entry
    };
    void adopt_userships(App_context& context);

    std::vector<Target>          m_targets;
    erhe::property::Property_set m_values;
    std::vector<Asset_reference> m_userships; // as Property_set_operation
    bool                         m_userships_adopted{false};
};

// Applies `state` (a value, an expression, or nullopt = clear) as the
// item's local layer and runs the editor consequence hook. Shared by the
// operations above and by direct (non-undoable) callers such as the startup
// script. An object value (D28) is applied only when the referenced item
// belongs to the target's scene or is a cross-scene referenceable asset;
// otherwise a warning names both items and nothing changes.
void apply_item_property(
    App_context&                                       context,
    erhe::Item_base&                                   item,
    const erhe::property::Dependency_property&         property,
    const std::optional<erhe::property::Local_state>&  state
);

// The same for a property of one of the item's sub-objects (D29):
// `target` is item.get_property_sub_object(index); a sealed item refuses
// the write, and the consequence hook runs with the item.
void apply_item_property(
    App_context&                                       context,
    erhe::Item_base&                                   item,
    erhe::property::Dependency_object&                 target,
    const erhe::property::Dependency_property&         property,
    const std::optional<erhe::property::Local_state>&  state
);

// D26: an undoable write of a writable computed property. The value goes
// through the property's setter now, and the returned operation records
// the stored property the setter writes (metadata `compute_writes`) with
// the local state it had before and has after, so undo restores exactly
// that state and execute re-applies it (idempotent). `target` is the item
// or its sub-object (D29). Null when the property is not a writable
// computed one, or the setter refused (a sealed item).
[[nodiscard]] auto make_computed_write_operation(
    const std::shared_ptr<erhe::Item_base>&      item,
    std::optional<std::size_t>                   sub_object,
    erhe::property::Dependency_object&           target,
    const erhe::property::Dependency_property&   property,
    const erhe::property::Property_value&        value
) -> std::shared_ptr<Property_set_operation>;

[[nodiscard]] auto to_local_state(const std::optional<erhe::property::Property_value>& value) -> std::optional<erhe::property::Local_state>;
[[nodiscard]] auto describe_local_state(const erhe::property::Dependency_property& property, const std::optional<erhe::property::Local_state>& state) -> std::string;

}
