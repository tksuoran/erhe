#pragma once

#include <vector>

namespace erhe::property { class Dependency_object; class Dependency_property; }

namespace editor {

enum class Developer_mode : unsigned int {
    hidden = 0, // developer_only registrations are left out
    shown  = 1
};

// The one place for the rules of the properties an object holds beyond its
// own class chain - attached properties (doc/property_system.md R7, D12,
// D13) and the secondary-type properties of a content-library folder (D30)
// - shared by the Properties window and the MCP property tools.

// D12 listing rule for one object. An attached property is listed when the
// object is of its holder type (Dependency_property::applies_to) and the
// registering type's visible_when holds for the object, or the object holds
// a local value for it (a stale hint stays visible and resettable). A
// secondary property (Property_registry::is_secondary_property) is listed
// when the object holds an own value for it (local, or from its style);
// its visible_when belongs to the secondary type's objects and is never
// evaluated on this one.
[[nodiscard]] auto is_extra_property_listed(
    const erhe::property::Dependency_object&   object,
    const erhe::property::Dependency_property& property
) -> bool;

// The registrations "Add Property" offers for the object: every attached
// property of its holder type and every secondary property the D12 rule
// does not list for it,
// in registry order; developer_only ones only in developer mode. Appends to
// `out` (the caller keeps the capacity).
void collect_addable_properties(
    const erhe::property::Dependency_object&                 object,
    Developer_mode                                           developer_mode,
    std::vector<const erhe::property::Dependency_property*>& out
);

}
