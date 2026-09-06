#include "erhe_item/typed.hpp"
#include "erhe_item/item_log.hpp"

#include <fmt/format.h>

namespace erhe {

Typed::Typed()           = default;
Typed::~Typed() noexcept = default;

Typed::Typed(const Typed& other) = default;

Typed& Typed::operator=(const Typed& other) = default;

Typed::Typed(const std::string_view name)
    : Item{name}
{
}

Typed::Typed(const std::string_view name, const std::string_view prim_type_name)
    : Item{name}
{
    set_prim_type_name(prim_type_name);
}

Typed::Typed(const Typed& src, for_clone)
    : Typed{src}
{
}

void Typed::handle_parent_update(Hierarchy* const old_parent, Hierarchy* const new_parent)
{
    Item_host* const old_item_host = (old_parent != nullptr) ? old_parent->get_item_host() : nullptr;
    Item_host* const new_item_host = (new_parent != nullptr) ? new_parent->get_item_host() : nullptr;
    if (old_item_host != new_item_host) {
        handle_item_host_update(old_item_host, new_item_host);
    }
}

void Typed::handle_item_host_update(Item_host* const old_item_host, Item_host* const new_item_host)
{
    static_cast<void>(old_item_host);
    set_item_host(new_item_host);
    for (const std::shared_ptr<Hierarchy>& child : get_children()) {
        // The hook is the prim class hierarchy's: a `Hierarchy` child that is
        // not a prim holds no item host of its own and has nothing to carry.
        Typed* const typed_child = dynamic_cast<Typed*>(child.get());
        if (typed_child != nullptr) {
            typed_child->handle_item_host_update(old_item_host, new_item_host);
        }
    }
}

auto Typed::get_prim_type_name() const -> std::string_view
{
    const std::string_view class_type_name = get_class_type_name();
    return class_type_name.empty() ? std::string_view{m_prim_type_name} : class_type_name;
}

void Typed::set_prim_type_name(const std::string_view prim_type_name)
{
    const std::string_view class_type_name = get_class_type_name();
    if (!class_type_name.empty()) {
        erhe::item::log->error(
            "type name of '{}' is fixed by its class to '{}'; '{}' rejected",
            get_name(), class_type_name, prim_type_name
        );
        return;
    }
    m_prim_type_name.assign(prim_type_name);
}

const erhe::property::Property<std::string> Typed::type_name_property = erhe::property::Property<std::string>::register_property(
    "type_name", Typed::property_owner_type(),
    erhe::property::Property_metadata{
        .ui     = erhe::property::Property_ui{.tooltip = "USD typeName token of the prim", .label = "Type Name"},
        .bridge = erhe::property::Property_bridge{
            .get = [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
                return std::string{static_cast<const Typed&>(object).get_prim_type_name()};
            },
            .set = [](erhe::property::Dependency_object& object, const erhe::property::Property_value& value) {
                static_cast<Typed&>(object).set_prim_type_name(std::get<std::string>(value));
            },
            // A class that fixes its own token holds no authored value, so
            // every write through the property path is refused here, the
            // way the sibling-unique name refusal lives in the name bridge.
            .validate = [](
                const erhe::property::Dependency_object& object,
                const erhe::property::Property_value&    value,
                std::string&                             out_error
            ) -> bool {
                const Typed&           item            = static_cast<const Typed&>(object);
                const std::string_view class_type_name = item.get_class_type_name();
                if (class_type_name.empty()) {
                    return true;
                }
                if (std::get<std::string>(value) == class_type_name) {
                    return true;
                }
                out_error = fmt::format(
                    "type name of '{}' is fixed by its class to '{}'", item.get_name(), class_type_name
                );
                return false;
            }
        }
    }
);

} // namespace erhe
