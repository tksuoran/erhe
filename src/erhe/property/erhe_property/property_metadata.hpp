#pragma once

#include "erhe_property/property_value.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <optional>
#include <string_view>

namespace erhe::property {

class Dependency_object;
class Dependency_property;

// Where the base value of a property on an object comes from.
enum class Value_source : uint8_t {
    default_value = 0, // metadata default
    inherited     = 1, // closest ancestor's effective value (inherits flag)
    local         = 2, // set_value on this object
    expression    = 3, // a formula on this object (D22); the local layer
    style         = 4, // the object's Property_style (D25); between local and inherited
    computed      = 5  // the owner's value provider (D26); read-only, no layers
};

[[nodiscard]] constexpr auto c_str(const Value_source source) -> const char*
{
    switch (source) {
        case Value_source::default_value: return "default";
        case Value_source::inherited:     return "inherited";
        case Value_source::local:         return "local";
        case Value_source::expression:    return "expression";
        case Value_source::style:         return "style";
        case Value_source::computed:      return "computed";
    }
    return "?";
}

class Property_changed_args
{
public:
    const Dependency_property& property;
    const Property_value&      old_value;
    const Property_value&      new_value;
    Value_source               old_source;
    Value_source               new_source;
};

using Property_changed_callback = std::function<void(Dependency_object&, const Property_changed_args&)>;
using Coerce_callback           = std::function<Property_value(const Dependency_object&, const Property_value&)>;
using Validate_callback         = std::function<bool(const Property_value&)>;
// Value provider of a computed property (doc/property-system.md D26):
// the effective value is whatever it returns, read on every get_value.
using Compute_callback          = std::function<Property_value(const Dependency_object&)>;
// Setter of a writable computed property (D26): set_value hands the value
// to it, and it writes the underlying stored property the provider derives
// its value from (a light's flux setter writes the intensity).
using Compute_set_callback      = std::function<void(Dependency_object&, const Property_value&)>;
// Per-object default of a property (doc/property-system.md D31): the
// default layer - below inherited, style and local - is whatever it
// returns for the object, so an item derives its own default from state
// it already holds (Item_base::purpose_property from the editor-only
// flag bits) and an authored value still overrides it.
using Compute_default_callback  = std::function<Property_value(const Dependency_object&)>;

// What a change of the property affects. Data only: the library never acts
// on these, the editor reads them (App_context::on_item_property_changed).
class Property_flags
{
public:
    static constexpr uint32_t none                         = 0u;
    static constexpr uint32_t affects_transform            = (1u << 0);
    static constexpr uint32_t affects_draw_list_partition  = (1u << 1);
    static constexpr uint32_t affects_shader_variant       = (1u << 2);
    static constexpr uint32_t serialize                    = (1u << 3);
    // D24: the write is accepted on a sealed object. For the property that
    // owns the seal (Item_base::lock_edit_property), which must stay
    // writable so the seal can be lifted through it.
    static constexpr uint32_t writable_when_sealed         = (1u << 4);
};

// What the Properties window needs to draw the row. Data only.
class Property_ui
{
public:
    enum class Presentation : uint8_t {
        plain = 0,
        color,          // vec3 / vec4 color edit
        angle_degrees,  // float / vec3 stored in radians, shown in degrees
        slider          // float / int slider within min..max
    };

    // Row shown only while this returns true for the inspected object
    // (e.g. a material's alpha cutoff only in the alpha-test blending
    // mode); unset = always shown.
    using Visible_when = std::function<bool(const Dependency_object&)>;

    std::optional<float> min           {};
    std::optional<float> max           {};
    std::optional<float> step          {};
    Presentation         presentation  {Presentation::plain};
    bool                 logarithmic   {false}; // slider presentation: logarithmic scale (D20)
    std::string_view     group         {};
    std::string_view     tooltip       {};
    bool                 developer_only{false};
    std::string_view     label         {}; // row label; empty = the property name
    Visible_when         visible_when  {};
    // Object properties (D28): the item type mask the row accepts as a
    // drop / picker candidate (erhe::Item_type bits, opaque here), and
    // whether the row offers a clear button.
    uint64_t             reference_item_types{0};
    bool                 show_clear_button   {true};
};

// Storage outside the object's entry store (doc/property-system.md
// D18): when `get` is bound, the property's local value is whatever `get`
// returns and set_value / clear_value go through `set`. The object never
// gets an entry for the property, it always reports Value_source::local,
// it never inherits, and its coerce callback runs on every read. For state
// that already has an engineered representation the object must keep
// (Node's transform with its deferred matrix decomposition).
class Property_bridge
{
public:
    std::function<Property_value(const Dependency_object&)>        get{};
    std::function<void(Dependency_object&, const Property_value&)> set{};
    // Optional object-level validation, run by Dependency_object::set_value
    // before anything is written and by callers that want the reason ahead of
    // the write (Dependency_object::validate_value). Unlike
    // Dependency_property::validate, which sees the value alone, this sees the
    // object, which is what a name that must be unique among siblings needs
    // (doc/usd-compatibility-plan.md M2).
    std::function<bool(const Dependency_object&, const Property_value&, std::string&)> validate{};

    [[nodiscard]] auto is_bound() const -> bool { return static_cast<bool>(get); }
};

class Property_metadata
{
public:
    // nullopt: the zero value of the property type (zero_value()), or the
    // first table entry for an enumeration.
    std::optional<Property_value> default_value   {};
    Property_changed_callback     property_changed{};
    Coerce_callback               coerce          {};
    bool                          inherits        {false};
    uint32_t                      flags           {Property_flags::serialize};
    Property_ui                   ui              {};
    Property_bridge               bridge          {};
    // D26: bound only on a computed property (registered read-only through
    // Property<T>::register_computed). The object has no entry for it, no
    // layer applies, and the owner pushes changes to expressions with
    // invalidate_dependents.
    Compute_callback              compute         {};
    // D26: a computed property with a setter is writable: set_value calls
    // `compute_set`, which writes `compute_writes` - the stored property the
    // value is derived from and the one an undoable edit of this property
    // records. Clearing and expressions stay rejected (no local layer).
    Compute_set_callback          compute_set     {};
    const Dependency_property*    compute_writes  {nullptr};
    // D31: bound when the default layer is per-object. `default_value`
    // stays the registration-time default (it carries the type check and
    // is what a reader without an object shows); `compute_default` is what
    // an object reads.
    Compute_default_callback      compute_default {};

    [[nodiscard]] auto is_computed         () const -> bool { return static_cast<bool>(compute); }
    [[nodiscard]] auto is_computed_writable() const -> bool { return static_cast<bool>(compute_set); }
    [[nodiscard]] auto has_computed_default() const -> bool { return static_cast<bool>(compute_default); }
};

} // namespace erhe::property
