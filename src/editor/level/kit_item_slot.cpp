#include "level/kit_item_slot.hpp"

#include "level/kit_shape_library.hpp"

#include "config/generated/inventory_slot.hpp"

#include <fmt/format.h>

namespace editor {

auto read_kit_item_slot(const Kit_shape_library& library, const Inventory_slot& slot) -> Kit_item_slot_reading
{
    if (slot.kit_shape.empty()) {
        return {};
    }
    Kit_parameter_values values;
    const std::string record_problem = read_kit_piece_record(library, slot.kit_shape, slot.kit_parameters, values);
    if (!record_problem.empty()) {
        return Kit_item_slot_reading{.item = std::nullopt, .problem = record_problem};
    }
    const Kit_shape_definition& shape = *values.get_shape();
    if (slot.kit_materials.size() > shape.material_slots.size()) {
        return Kit_item_slot_reading{
            .item    = std::nullopt,
            .problem = fmt::format("{} materials for the {} material slots of {}", slot.kit_materials.size(), shape.material_slots.size(), shape.id)
        };
    }
    for (const std::string& name : slot.kit_materials) {
        if (!name.empty() && (library.find_palette_material(name) == nullptr)) {
            return Kit_item_slot_reading{.item = std::nullopt, .problem = fmt::format("the kit palette has no material '{}'", name)};
        }
    }
    return Kit_item_slot_reading{.item = Kit_item{values, slot.kit_materials}, .problem = {}};
}

void write_kit_item_slot(const Kit_item& item, Inventory_slot& out_slot)
{
    const Kit_shape_definition* const shape = item.get_shape();
    if (shape == nullptr) {
        out_slot.kit_shape.clear();
        out_slot.kit_parameters.clear();
        out_slot.kit_materials.clear();
        return;
    }
    out_slot.kit_shape      = shape->id;
    out_slot.kit_parameters = item.get_values().to_canonical_text();
    out_slot.kit_materials  = item.get_slot_materials();
}

auto make_kit_item_tooltip(const Kit_item& item) -> std::string
{
    const Kit_shape_definition* const shape = item.get_shape();
    if (shape == nullptr) {
        return {};
    }
    const Kit_parameter_values& values = item.get_values();
    std::string text = fmt::format("{}\n{}", shape->display_name, shape->id);
    for (std::size_t i = 0, end = shape->parameters.size(); i < end; ++i) {
        const Kit_parameter&       parameter = shape->parameters[i];
        const Kit_parameter_value& value     = values.get_values()[i];
        const char* const          derived   = (values.get_value_source(i) == Kit_value_source::seed) ? " (seed)" : "";
        switch (parameter.type) {
            case Kit_parameter_type::float_value: {
                text += fmt::format("\n{}: {:.3f} {}{}", parameter.name, value.float_value, c_str(parameter.unit), derived);
                break;
            }
            case Kit_parameter_type::int_value: {
                text += fmt::format("\n{}: {}{}", parameter.name, value.int_value, derived);
                break;
            }
            case Kit_parameter_type::enum_value: {
                const bool in_range = (value.int_value >= 0) && (static_cast<std::size_t>(value.int_value) < parameter.enum_values.size());
                text += fmt::format("\n{}: {}{}", parameter.name, in_range ? parameter.enum_values[static_cast<std::size_t>(value.int_value)] : std::string{"?"}, derived);
                break;
            }
        }
    }
    const std::vector<std::string>& materials = item.get_slot_materials();
    for (std::size_t slot = 0, end = shape->material_slots.size(); slot < end; ++slot) {
        const bool named = (slot < materials.size()) && !materials[slot].empty();
        text += fmt::format("\n{} material: {}", shape->material_slots[slot], named ? materials[slot] : std::string{"(default)"});
    }
    return text;
}

} // namespace editor
