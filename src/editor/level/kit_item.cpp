#include "level/kit_item.hpp"

namespace editor {

Kit_item::Kit_item(const Kit_parameter_values& values, const std::span<const std::string> slot_materials)
    : m_values        {values}
    , m_slot_materials{slot_materials.begin(), slot_materials.end()}
{
}

auto Kit_item::get_shape() const -> const Kit_shape_definition*
{
    return m_values.get_shape();
}

auto Kit_item::get_values() const -> const Kit_parameter_values&
{
    return m_values;
}

auto Kit_item::get_slot_materials() const -> const std::vector<std::string>&
{
    return m_slot_materials;
}

auto Kit_item::get_display_name() const -> const std::string&
{
    static const std::string empty{};
    const Kit_shape_definition* const shape = m_values.get_shape();
    return (shape != nullptr) ? shape->display_name : empty;
}

auto Kit_item::validate() const -> std::string
{
    if (m_values.get_shape() == nullptr) {
        return "the kit item has no shape";
    }
    return m_values.validate();
}

auto Kit_item::is_valid() const -> bool
{
    return validate().empty();
}

} // namespace editor
