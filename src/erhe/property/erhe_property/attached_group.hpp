#pragma once

#include "erhe_property/property_metadata.hpp"

namespace erhe::property {

class Dependency_object;
class Dependency_property;

// Attached value groups (doc/erhe/property_system.md section 4.23): a set of
// attached properties registered by one class on one holder class, one of
// them the group's KEY property. An object carries the group exactly while
// the key property's effective value on it differs from that object's own
// default layer (D31), and every other value of the group is listed on the
// objects that carry it.

// True when `object` carries the group keyed on `key`.
[[nodiscard]] auto carries_attached_group(const Dependency_object& object, const Dependency_property& key) -> bool;

// The Property_ui::visible_when of every non-key value of a group keyed on
// `key`: carries_attached_group. The key property is registered first, so
// its registration is complete when the rest of the group takes this.
[[nodiscard]] auto attached_group_visible_when(const Dependency_property& key) -> Property_ui::Visible_when;

// The same, narrowed to the objects `holder_predicate` accepts - for a group
// whose rows belong on some of the holder class's objects only.
[[nodiscard]] auto attached_group_visible_when(
    const Dependency_property&  key,
    Property_ui::Visible_when   holder_predicate
) -> Property_ui::Visible_when;

// The D12 listing rule for an attached property: the object is of the
// property's holder type (Dependency_property::applies_to) and the
// registering type's visible_when holds for it, or the object holds a local
// value for it (a stale value stays visible and resettable). False for a
// property that is not attached. The editor's is_extra_property_listed
// answers the attached half of its question here.
[[nodiscard]] auto is_attached_property_listed(
    const Dependency_object&   object,
    const Dependency_property& property
) -> bool;

} // namespace erhe::property
