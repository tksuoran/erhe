#include "erhe_scene/instance_override.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene_log.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_string.hpp"

#include <cmath>
#include <optional>
#include <string_view>

namespace erhe::scene {

namespace {

[[nodiscard]] auto is_near(const glm::mat4& lhs, const glm::mat4& rhs) -> bool
{
    constexpr float tolerance = 1e-5f;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            if (std::abs(lhs[j][i] - rhs[j][i]) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

// The transform half of the override rule: a bridged transform always holds a
// local value, so only a difference from the counterpart is an override.
[[nodiscard]] auto is_transform_overridden(const erhe::Item_base& item, const erhe::property::Dependency_object& counterpart) -> bool
{
    const Xformable* xformable             = dynamic_cast<const Xformable*>(&item);
    const Xformable* counterpart_xformable = dynamic_cast<const Xformable*>(&counterpart);
    if ((xformable == nullptr) || (counterpart_xformable == nullptr)) {
        return false;
    }
    return !is_near(
        xformable->parent_from_node_transform().get_matrix(),
        counterpart_xformable->parent_from_node_transform().get_matrix()
    );
}

void collect_item(
    const erhe::Hierarchy&              item,
    const std::string&                  relative_path,
    std::vector<Instance_override_item>& out_items
)
{
    const std::shared_ptr<const erhe::property::Dependency_object>& counterpart = item.get_reference();
    if (!counterpart) {
        return; // not instance content: parented under the carrier by hand
    }

    const erhe::property::Owner_type owner_type = item.get_property_owner_type();
    bool                             has_value  = false;
    item.for_each_local_value(
        [&has_value, &item, owner_type](
            const erhe::property::Dependency_property& property,
            const erhe::property::Property_value&      value
        ) {
            static_cast<void>(value);
            if (has_value) {
                return;
            }
            const erhe::property::Property_metadata& metadata = property.get_metadata(owner_type);
            if ((metadata.flags & erhe::property::Property_flags::serialize) == 0u) {
                return;
            }
            if (metadata.bridge.is_bound() || metadata.is_computed()) {
                return;
            }
            if (item.get_expression(property).has_value()) {
                return;
            }
            has_value = true;
        }
    );

    const bool transform_overridden = is_transform_overridden(item, *counterpart.get());
    if (has_value || transform_overridden) {
        out_items.push_back(
            Instance_override_item{
                .relative_path        = relative_path,
                .item                 = &item,
                .transform_overridden = transform_overridden
            }
        );
    }

    for (const std::shared_ptr<erhe::Hierarchy>& child : item.get_children()) {
        if (!child) {
            continue;
        }
        const std::string child_path = relative_path.empty()
            ? child->get_name()
            : (relative_path + "/" + child->get_name());
        collect_item(*child.get(), child_path, out_items);
    }
}

// The property one override value names. A collected override spells the name
// the way the registry does (`Owner.name` only where the object holds the
// value for another class), while a file can spell it qualified whatever the
// object is - `erhe:Mesh:shadow_cast` on a Mesh prim - so a qualified name
// that resolves to nothing is retried as the bare name, with the owner type
// it names checked against the property's own.
[[nodiscard]] auto find_override_property(
    const erhe::property::Dependency_object& object,
    const std::string&                       name
) -> const erhe::property::Dependency_property*
{
    const erhe::property::Property_registry&   registry = erhe::property::Property_registry::get();
    const erhe::property::Dependency_property* property = registry.find_for_object(object, name);
    if (property != nullptr) {
        return property;
    }
    const std::size_t dot = name.find('.');
    if (dot == std::string::npos) {
        return nullptr;
    }
    const std::optional<erhe::property::Owner_type> named_owner = registry.find_owner_type(std::string_view{name}.substr(0, dot));
    if (!named_owner.has_value()) {
        return nullptr;
    }
    property = registry.find_for_object(object, std::string_view{name}.substr(dot + 1));
    if ((property != nullptr) && (property->get_owner_type() != named_owner.value())) {
        return nullptr;
    }
    return property;
}

} // anonymous namespace

void apply_property_values(
    erhe::Item_base&                            item,
    const std::vector<Instance_override_value>& values,
    const std::string_view                      owner
)
{
    for (const Instance_override_value& value : values) {
        const erhe::property::Dependency_property* property = find_override_property(item, value.name);
        if (property == nullptr) {
            log->warn("'{}': the value '{}' names no property of '{}'", owner, value.name, item.get_name());
            continue;
        }
        const std::optional<erhe::property::Property_value> parsed =
            erhe::property::parse_value(item, *property, value.text);
        if (!parsed.has_value()) {
            log->warn("'{}': the value '{}' text '{}' does not parse", owner, value.name, value.text);
            continue;
        }
        item.set_value(*property, parsed.value());
    }
}

auto collect_instance_override_items(const erhe::Hierarchy& carrier) -> std::vector<Instance_override_item>
{
    std::vector<Instance_override_item> result;
    for (const std::shared_ptr<erhe::Hierarchy>& clone : carrier.get_children()) {
        if (clone) {
            collect_item(*clone.get(), std::string{}, result);
        }
    }
    return result;
}

auto collect_instance_overrides(const erhe::Hierarchy& carrier) -> std::vector<Instance_override>
{
    std::vector<Instance_override>            result;
    const std::vector<Instance_override_item> items = collect_instance_override_items(carrier);
    for (const Instance_override_item& entry : items) {
        const erhe::Item_base* item = entry.item;
        Instance_override      override_entry{};
        override_entry.relative_path = entry.relative_path;

        const erhe::property::Owner_type owner_type = item->get_property_owner_type();
        item->for_each_local_value(
            [&override_entry, item, owner_type](
                const erhe::property::Dependency_property& property,
                const erhe::property::Property_value&      value
            ) {
                const erhe::property::Property_metadata& metadata = property.get_metadata(owner_type);
                if ((metadata.flags & erhe::property::Property_flags::serialize) == 0u) {
                    return;
                }
                if (metadata.bridge.is_bound() || metadata.is_computed()) {
                    return;
                }
                if (item->get_expression(property).has_value()) {
                    return;
                }
                override_entry.values.push_back(
                    Instance_override_value{
                        .name = erhe::property::Property_registry::get().qualified_name(*item, property),
                        .text = erhe::property::to_string(property, value)
                    }
                );
            }
        );

        override_entry.transform_overridden = entry.transform_overridden;
        const Xformable* xformable = dynamic_cast<const Xformable*>(item);
        if (entry.transform_overridden && (xformable != nullptr)) {
            override_entry.transform      = xformable->parent_from_node_transform().get_matrix();
            override_entry.xform_op_stack = xformable->copy_xform_op_stack();
        }
        result.push_back(std::move(override_entry));
    }
    return result;
}

void apply_instance_overrides(erhe::Hierarchy& carrier, const std::vector<Instance_override>& overrides)
{
    for (const Instance_override& entry : overrides) {
        erhe::Hierarchy* target = nullptr;
        for (const std::shared_ptr<erhe::Hierarchy>& clone : carrier.get_children()) {
            if (!clone) {
                continue;
            }
            target = erhe::find_by_path(*clone.get(), entry.relative_path);
            if (target != nullptr) {
                break;
            }
        }
        if (target == nullptr) {
            log->warn(
                "instance '{}': the override of '{}' names no item of the instance",
                carrier.get_name(),
                entry.relative_path
            );
            continue;
        }
        apply_property_values(*target, entry.values, carrier.get_name());
        if (entry.transform_overridden) {
            Xformable* xformable = dynamic_cast<Xformable*>(target);
            if (xformable == nullptr) {
                log->warn("instance '{}': the transform override of '{}' names an item without one", carrier.get_name(), entry.relative_path);
                continue;
            }
            if (entry.xform_op_stack.has_value()) {
                xformable->set_xform_op_stack(entry.xform_op_stack.value());
            } else {
                xformable->set_parent_from_node(entry.transform);
            }
        }
    }
}

} // namespace erhe::scene
