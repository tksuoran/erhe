#include "level/kit_standard_property.hpp"

namespace editor {

auto c_str(const Kit_standard_property property) -> const char*
{
    switch (property) {
        case Kit_standard_property::room_height:     return "room.height";
        case Kit_standard_property::floor_thickness: return "floor.thickness";
        case Kit_standard_property::wall_thickness:  return "wall.thickness";
        case Kit_standard_property::child_length:    return "child.length";
        case Kit_standard_property::floor_material:  return "floor.material";
        case Kit_standard_property::wall_material:   return "wall.material";
        default:                                     return "?";
    }
}

auto get_kind(const Kit_standard_property property) -> Kit_property_kind
{
    return ((property == Kit_standard_property::floor_material) || (property == Kit_standard_property::wall_material))
        ? Kit_property_kind::material
        : Kit_property_kind::length;
}

auto parse_standard_property(const std::string_view name) -> std::optional<Kit_standard_property>
{
    for (unsigned int i = 0; i < c_standard_property_count; ++i) {
        const Kit_standard_property property = static_cast<Kit_standard_property>(i);
        if (name == c_str(property)) {
            return property;
        }
    }
    return std::nullopt;
}

void Kit_supply_list::add(const Kit_supplied_value& value)
{
    for (std::size_t i = 0; i < m_count; ++i) {
        if (m_values[i].property == value.property) {
            m_values[i] = value;
            return;
        }
    }
    if (m_count < m_values.size()) {
        m_values[m_count] = value;
        ++m_count;
    }
}

auto Kit_supply_list::find(const Kit_standard_property property) const -> const Kit_supplied_value*
{
    for (std::size_t i = 0; i < m_count; ++i) {
        if (m_values[i].property == property) {
            return &m_values[i];
        }
    }
    return nullptr;
}

} // namespace editor
