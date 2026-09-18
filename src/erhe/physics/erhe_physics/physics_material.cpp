#include "erhe_physics/physics_material.hpp"

#include <algorithm>

namespace erhe::physics {

namespace {

using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;

constexpr erhe::property::Enum_entry c_combine_mode_entries[] = {
    {"Average",  static_cast<int32_t>(Combine_mode::e_average)},
    {"Minimum",  static_cast<int32_t>(Combine_mode::e_minimum)},
    {"Maximum",  static_cast<int32_t>(Combine_mode::e_maximum)},
    {"Multiply", static_cast<int32_t>(Combine_mode::e_multiply)},
};

const erhe::property::Owner_type c_owner = Physics_material::property_owner_type();

// Friction has no upper bound in the physics sense (and none in the glTF
// extension), so only negative values are rejected; restitution above 1
// adds energy at every contact and is rejected.
auto non_negative(const Property_value& value) -> bool
{
    return std::get<float>(value) >= 0.0f;
}

auto unit_range(const Property_value& value) -> bool
{
    const float f = std::get<float>(value);
    return (f >= 0.0f) && (f <= 1.0f);
}

auto positive(const Property_value& value) -> bool
{
    return std::get<float>(value) > 0.0f;
}

auto slider(const float min, const float max, const std::string_view label, const std::string_view tooltip) -> Property_ui
{
    return Property_ui{.min = min, .max = max, .presentation = Property_ui::Presentation::slider, .tooltip = tooltip, .label = label};
}

constexpr std::string_view c_combine_tooltip = "Contact pair precedence: average, then minimum, then maximum, then multiply";

} // anonymous namespace

const erhe::property::Enum_info c_combine_mode_enum_info{"Combine_mode", c_combine_mode_entries};

// Every value inherits (doc/erhe/property_system.md D30): a Physics Materials
// folder or a style holds them for the materials below it.
const Property<float> Physics_material::static_friction_property = Property<float>::register_property(
    "static_friction", c_owner,
    Property_metadata{.default_value = c_default_friction, .inherits = true, .ui = slider(0.0f, 1.0f, "Static Friction", "Combined with the other body's material by the friction combine mode")},
    non_negative
);
const Property<float> Physics_material::dynamic_friction_property = Property<float>::register_property(
    "dynamic_friction", c_owner,
    Property_metadata{.default_value = c_default_friction, .inherits = true, .ui = slider(0.0f, 1.0f, "Dynamic Friction", "Combined with the other body's material by the friction combine mode")},
    non_negative
);
const Property<float> Physics_material::restitution_property = Property<float>::register_property(
    "restitution", c_owner,
    Property_metadata{.default_value = c_default_restitution, .inherits = true, .ui = slider(0.0f, 1.0f, "Restitution", "Bounciness; combined with the other body's material by the restitution combine mode")},
    unit_range
);
const Property<Combine_mode> Physics_material::friction_combine_property = Property<Combine_mode>::register_property(
    "friction_combine", c_owner, c_combine_mode_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Combine_mode::e_average), .inherits = true, .ui = Property_ui{.tooltip = c_combine_tooltip, .label = "Friction Combine"}}
);
const Property<Combine_mode> Physics_material::restitution_combine_property = Property<Combine_mode>::register_property(
    "restitution_combine", c_owner, c_combine_mode_enum_info,
    Property_metadata{.default_value = erhe::property::make_value(Combine_mode::e_average), .inherits = true, .ui = Property_ui{.tooltip = c_combine_tooltip, .label = "Restitution Combine"}}
);
const Property<float> Physics_material::linear_damping_property = Property<float>::register_property(
    "linear_damping", c_owner,
    Property_metadata{.default_value = c_default_linear_damping, .inherits = true, .ui = slider(0.0f, 1.0f, "Linear Damping", "Fraction of linear velocity lost per second by every body of this material")},
    unit_range
);
const Property<float> Physics_material::angular_damping_property = Property<float>::register_property(
    "angular_damping", c_owner,
    Property_metadata{.default_value = c_default_angular_damping, .inherits = true, .ui = slider(0.0f, 1.0f, "Angular Damping", "Fraction of angular velocity lost per second by every body of this material")},
    unit_range
);
const Property<float> Physics_material::wind_receptivity_property = Property<float>::register_property(
    "wind_receptivity", c_owner,
    Property_metadata{.default_value = c_default_wind_receptivity, .inherits = true, .ui = slider(0.0f, 10.0f, "Wind Receptivity", "kg/s: force = wind_receptivity * (wind velocity - body velocity) at the center of mass each fixed step; 0 = unaffected by wind")},
    non_negative
);
const Property<float> Physics_material::density_property = Property<float>::register_property(
    "density", c_owner,
    Property_metadata{.default_value = c_default_density, .inherits = true, .ui = Property_ui{.min = 0.01f, .max = 100.0f, .presentation = Property_ui::Presentation::slider, .logarithmic = true, .tooltip = "Mass per shape volume of a body without an explicit mass", .label = "Density"}},
    positive
);

auto combine(const Combine_mode a, const Combine_mode b) -> Combine_mode
{
    // Spec precedence: average > minimum > maximum > multiply.
    // Combine_mode values are ordered so that the higher-precedence mode has
    // the lower integer value, so the pair mode is simply the minimum.
    return static_cast<Combine_mode>(std::min(static_cast<int>(a), static_cast<int>(b)));
}

auto combine_values(const Combine_mode mode, const float a, const float b) -> float
{
    switch (mode) {
        case Combine_mode::e_average:  return 0.5f * (a + b);
        case Combine_mode::e_minimum:  return std::min(a, b);
        case Combine_mode::e_maximum:  return std::max(a, b);
        case Combine_mode::e_multiply: return a * b;
        default:                       return 0.5f * (a + b);
    }
}

Physics_material::Physics_material()                                   = default;
Physics_material::Physics_material(const Physics_material&)            = default;
Physics_material& Physics_material::operator=(const Physics_material&) = default;
Physics_material::~Physics_material() noexcept                         = default;

Physics_material::Physics_material(const std::string_view name)
    : Item{name}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

} // namespace erhe::physics
