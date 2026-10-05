#include "operations/item_property_apply.hpp"
#include "app_context.hpp"
#include "editor_log.hpp"

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_value.hpp"

namespace editor {

auto get_referenced_item(const std::optional<erhe::property::Local_state>& state) -> std::shared_ptr<erhe::Item_base>
{
    if (!state.has_value()) {
        return {};
    }
    const erhe::property::Property_value* value = std::get_if<erhe::property::Property_value>(&state.value());
    if (value == nullptr) {
        return {};
    }
    return std::dynamic_pointer_cast<erhe::Item_base>(erhe::property::get_referenced_object(*value));
}

auto is_sealed_sub_object(const erhe::Item_base& item, const erhe::property::Dependency_object& target) -> bool
{
    return (&target != static_cast<const erhe::property::Dependency_object*>(&item)) && item.is_sealed();
}

auto apply_item_property(
    App_context&                                       context,
    erhe::Item_base&                                   item,
    const erhe::property::Dependency_property&         property,
    const std::optional<erhe::property::Local_state>&  state
) -> bool
{
    return apply_item_property(context, item, item, property, state);
}

auto apply_item_property(
    App_context&                                       context,
    erhe::Item_base&                                   item,
    erhe::property::Dependency_object&                 target,
    const erhe::property::Dependency_property&         property,
    const std::optional<erhe::property::Local_state>&  state
) -> bool
{
    if (is_sealed_sub_object(item, target)) {
        log_operations->warn("property '{}' on a sub-object of sealed '{}' not applied", property.get_name(), item.get_name());
        return false;
    }
    if (const std::shared_ptr<erhe::Item_base> referenced = get_referenced_item(state); referenced && !is_item_reference_allowed(context, item, *referenced)) {
        return false;
    }
    if (!target.apply_local_state(property, state)) {
        // Sealed item (D24) or a rejected value: the store logged why.
        log_operations->warn("property '{}' on '{}' not applied", property.get_name(), item.get_name());
        return false;
    }
    context.on_item_property_changed(item, property);
    return true;
}

auto apply_item_property(
    App_context&                                       context,
    erhe::Item_base&                                   item,
    const std::optional<std::size_t>&                  sub_object,
    const erhe::property::Dependency_property&         property,
    const std::optional<erhe::property::Local_state>&  state
) -> bool
{
    if (!sub_object.has_value()) {
        return apply_item_property(context, item, item, property, state);
    }
    erhe::property::Dependency_object* target = item.get_property_sub_object(sub_object.value());
    if (target == nullptr) {
        log_operations->warn("property '{}' on '{}': sub-object {} no longer exists, not applied", property.get_name(), item.get_name(), sub_object.value());
        return false;
    }
    return apply_item_property(context, item, *target, property, state);
}

}
