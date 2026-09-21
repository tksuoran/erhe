#include "erhe_property/attached_group.hpp"

#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"

namespace erhe::property {

auto carries_attached_group(const Dependency_object& object, const Dependency_property& key) -> bool
{
    return object.get_value(key) != object.get_default_value(key);
}

auto attached_group_visible_when(const Dependency_property& key) -> Property_ui::Visible_when
{
    const Dependency_property* const key_property = &key;
    return [key_property](const Dependency_object& object) -> bool {
        return carries_attached_group(object, *key_property);
    };
}

auto attached_group_visible_when(
    const Dependency_property&      key,
    const Property_ui::Visible_when holder_predicate
) -> Property_ui::Visible_when
{
    const Dependency_property* const key_property = &key;
    return [key_property, holder_predicate](const Dependency_object& object) -> bool {
        return
            ((!holder_predicate) || holder_predicate(object)) &&
            carries_attached_group(object, *key_property);
    };
}

auto is_attached_property_listed(const Dependency_object& object, const Dependency_property& property) -> bool
{
    if (!property.is_attached()) {
        return false;
    }
    const Owner_type object_type = object.get_property_owner_type();
    if (!property.applies_to(object_type)) {
        return false;
    }
    const Property_ui::Visible_when& visible_when = property.get_metadata(object_type).ui.visible_when;
    return (visible_when && visible_when(object)) || object.has_local_value(property);
}

} // namespace erhe::property
