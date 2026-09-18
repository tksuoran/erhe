#pragma once

#include "erhe_property/dependency_object.hpp"
#include "erhe_property/property_set.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace erhe::property {

// A named style source (D25 in doc/property_system.md; WPF Style
// setters): a Dependency_object whose local values are the style, filled
// from a Property_set at construction and editable afterwards like any
// object - a change reaches every object using it. Shared between users
// through std::shared_ptr<const Property_style>. The editor's style items
// are Dependency_objects of their own (doc/style_library.md D2); this
// class serves tests and non-item users.
class Property_style : public Dependency_object
{
public:
    Property_style(std::string name, const Property_set& values)
        : m_name{std::move(name)}
    {
        for (const Property_set::Entry& entry : values.entries()) {
            set_value(*entry.property, entry.value);
        }
    }

    [[nodiscard]] auto get_name  () const -> std::string_view { return m_name; }
    [[nodiscard]] auto get_values() const -> Property_set     { return Property_set::read_local_values(*this); }

    // Implements Dependency_object: the name is the text form of a
    // reference to this style.
    [[nodiscard]] auto get_reference_path() const -> std::string override { return m_name; }

private:
    std::string m_name;
};

} // namespace erhe::property
