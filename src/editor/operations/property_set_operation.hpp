#pragma once

#include "assets/asset_reference.hpp"
#include "operations/item_property_apply.hpp"
#include "operations/operation.hpp"

#include "erhe_property/expression.hpp"
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
//
// Follow-ups: the first execute that applies the write records the
// operations the edit implies on other items (the connected child bones a
// Rig.tail or Rig.connected edit moves, rig/bone_connect.hpp) and runs them;
// a redo runs them again after the write and an undo undoes them before
// restoring the value, so the edit and its consequences are one undo step.
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
    auto apply(App_context& context, const std::optional<erhe::property::Local_state>& state) -> bool;
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
    std::vector<std::shared_ptr<Operation>>     m_follow_ups;
    bool                                        m_follow_ups_recorded{false};
};

// Asset-manager plan R5.4: an operation that holds `item` (a managed asset
// named by an object value it can restore) declares the usership; one
// entry per distinct asset. No-op for a null item, a non-asset item or
// without an asset manager.
void adopt_reference_usership(App_context& context, std::vector<Asset_reference>& userships, const std::shared_ptr<erhe::Item_base>& item);

[[nodiscard]] auto to_local_state(const std::optional<erhe::property::Property_value>& value) -> std::optional<erhe::property::Local_state>;
[[nodiscard]] auto describe_local_state(const erhe::property::Dependency_property& property, const std::optional<erhe::property::Local_state>& state) -> std::string;

}
