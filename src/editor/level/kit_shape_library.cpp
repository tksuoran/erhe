#include "level/kit_shape_library.hpp"
#include "level/kit_shapes.hpp"

#include <utility>

namespace editor {

auto make_length_parameter(
    const char*             name,
    const char*             description,
    const float             default_value,
    const Kit_default_scale default_scale,
    const float             min_value,
    const float             max_value
) -> Kit_parameter
{
    Kit_parameter parameter;
    parameter.name          = name;
    parameter.description   = description;
    parameter.type          = Kit_parameter_type::float_value;
    parameter.unit          = Kit_unit::meters;
    parameter.default_scale = default_scale;
    parameter.float_default = default_value;
    parameter.float_min     = min_value;
    parameter.float_max     = max_value;
    return parameter;
}

auto make_ratio_parameter(const char* name, const char* description, const float default_value) -> Kit_parameter
{
    Kit_parameter parameter;
    parameter.name          = name;
    parameter.description   = description;
    parameter.type          = Kit_parameter_type::float_value;
    parameter.unit          = Kit_unit::ratio;
    parameter.default_scale = Kit_default_scale::meters;
    parameter.float_default = default_value;
    parameter.float_min     = 0.0f;
    parameter.float_max     = 1.0f;
    return parameter;
}

auto make_count_parameter(const char* name, const char* description, const int default_value, const int min_value, const int max_value) -> Kit_parameter
{
    Kit_parameter parameter;
    parameter.name        = name;
    parameter.description = description;
    parameter.type        = Kit_parameter_type::int_value;
    parameter.unit        = Kit_unit::count;
    parameter.int_default = default_value;
    parameter.int_min     = min_value;
    parameter.int_max     = max_value;
    return parameter;
}

auto make_seed_parameter() -> Kit_parameter
{
    Kit_parameter parameter;
    parameter.name        = "seed";
    parameter.description = "Random seed; equal seeds give equal meshes and equal derived values";
    parameter.type        = Kit_parameter_type::int_value;
    parameter.unit        = Kit_unit::none;
    parameter.int_default = 1;
    parameter.int_min     = 0;
    parameter.int_max     = kit_seed_max;
    return parameter;
}

auto make_enum_parameter(const char* name, const char* description, std::vector<std::string> value_names, const int default_index) -> Kit_parameter
{
    Kit_parameter parameter;
    parameter.name        = name;
    parameter.description = description;
    parameter.type        = Kit_parameter_type::enum_value;
    parameter.unit        = Kit_unit::none;
    parameter.int_default = default_index;
    parameter.int_min     = 0;
    parameter.int_max     = static_cast<int>(value_names.size()) - 1;
    parameter.enum_values = std::move(value_names);
    return parameter;
}

auto all_sides() -> std::vector<Kit_side>
{
    return {Kit_side::x_neg, Kit_side::x_pos, Kit_side::y_neg, Kit_side::y_pos, Kit_side::z_neg, Kit_side::z_pos};
}

auto derive_float(Kit_parameter parameter, const float min_value, const float max_value, const float step) -> Kit_parameter
{
    parameter.derive            = Kit_derive::derivable;
    parameter.derive_float_min  = min_value;
    parameter.derive_float_max  = max_value;
    parameter.derive_float_step = step;
    return parameter;
}

auto derive_int(Kit_parameter parameter, const int min_value, const int max_value) -> Kit_parameter
{
    parameter.derive         = Kit_derive::derivable;
    parameter.derive_int_min = min_value;
    parameter.derive_int_max = max_value;
    return parameter;
}

auto derive_enum(Kit_parameter parameter) -> Kit_parameter
{
    parameter.derive         = Kit_derive::derivable;
    parameter.derive_int_min = 0;
    parameter.derive_int_max = static_cast<int>(parameter.enum_values.size()) - 1;
    return parameter;
}

auto from_seed_by_default(Kit_parameter parameter) -> Kit_parameter
{
    parameter.default_source = Kit_value_source::seed;
    return parameter;
}

Kit_shape_library::Kit_shape_library()
{
    m_kits = {
        Kit_definition{.name = "basic",        .display_name = "Basic",        .triangle_ceiling = 500},
        Kit_definition{.name = "architecture", .display_name = "Architecture", .triangle_ceiling = 500},
        Kit_definition{.name = "nature",       .display_name = "Nature",       .triangle_ceiling = 500}
    };
    add_basic_shapes       (m_shapes);
    add_architecture_shapes(m_shapes);
    add_nature_shapes      (m_shapes);
    // Base colors are linear.
    m_palette = {
        Kit_palette_material{.name = "Kit Stone",          .base_color = glm::vec3{0.50f, 0.50f, 0.47f}, .roughness = 0.85f},
        Kit_palette_material{.name = "Kit Floor",          .base_color = glm::vec3{0.42f, 0.34f, 0.26f}, .roughness = 0.75f},
        Kit_palette_material{.name = "Kit Plaster",        .base_color = glm::vec3{0.78f, 0.72f, 0.60f}, .roughness = 0.90f},
        Kit_palette_material{.name = "Kit Trim",           .base_color = glm::vec3{0.60f, 0.56f, 0.50f}, .roughness = 0.80f},
        Kit_palette_material{.name = "Kit Wood",           .base_color = glm::vec3{0.45f, 0.29f, 0.15f}, .roughness = 0.70f},
        Kit_palette_material{.name = "Kit Rock",           .base_color = glm::vec3{0.36f, 0.35f, 0.34f}, .roughness = 0.95f},
        Kit_palette_material{.name = "Kit Earth",          .base_color = glm::vec3{0.36f, 0.24f, 0.14f}, .roughness = 0.95f},
        Kit_palette_material{.name = "Kit Grass",          .base_color = glm::vec3{0.22f, 0.42f, 0.12f}, .roughness = 0.90f},
        Kit_palette_material{.name = "Kit Foliage",        .base_color = glm::vec3{0.10f, 0.32f, 0.08f}, .roughness = 0.85f},
        Kit_palette_material{.name = "Kit Autumn Foliage", .base_color = glm::vec3{0.48f, 0.22f, 0.05f}, .roughness = 0.85f},
        Kit_palette_material{.name = "Kit Bark",           .base_color = glm::vec3{0.22f, 0.15f, 0.09f}, .roughness = 0.95f}
    };
    // One default per slot name and a few alternatives (R2.9).
    m_slot_palettes = {
        Kit_slot_palette{.slot = "stone",   .default_material = "Kit Stone",   .alternatives = {"Kit Rock", "Kit Plaster", "Kit Wood"}},
        Kit_slot_palette{.slot = "floor",   .default_material = "Kit Floor",   .alternatives = {"Kit Wood", "Kit Stone", "Kit Earth"}},
        Kit_slot_palette{.slot = "wall",    .default_material = "Kit Plaster", .alternatives = {"Kit Stone", "Kit Wood", "Kit Trim"}},
        Kit_slot_palette{.slot = "trim",    .default_material = "Kit Trim",    .alternatives = {"Kit Wood", "Kit Stone", "Kit Plaster"}},
        Kit_slot_palette{.slot = "rock",    .default_material = "Kit Rock",    .alternatives = {"Kit Stone", "Kit Earth"}},
        Kit_slot_palette{.slot = "earth",   .default_material = "Kit Earth",   .alternatives = {"Kit Grass", "Kit Rock", "Kit Stone"}},
        Kit_slot_palette{.slot = "grass",   .default_material = "Kit Grass",   .alternatives = {"Kit Earth", "Kit Foliage"}},
        Kit_slot_palette{.slot = "foliage", .default_material = "Kit Foliage", .alternatives = {"Kit Autumn Foliage", "Kit Grass"}},
        Kit_slot_palette{.slot = "bark",    .default_material = "Kit Bark",    .alternatives = {"Kit Wood", "Kit Rock"}}
    };
}

auto Kit_shape_library::get_kits() const -> const std::vector<Kit_definition>&
{
    return m_kits;
}

auto Kit_shape_library::get_shapes() const -> const std::vector<Kit_shape_definition>&
{
    return m_shapes;
}

auto Kit_shape_library::find(const std::string_view id) const -> const Kit_shape_definition*
{
    for (const Kit_shape_definition& shape : m_shapes) {
        if (shape.id == id) {
            return &shape;
        }
    }
    return nullptr;
}

auto Kit_shape_library::find_kit(const std::string_view name) const -> const Kit_definition*
{
    for (const Kit_definition& kit : m_kits) {
        if (kit.name == name) {
            return &kit;
        }
    }
    return nullptr;
}

auto Kit_shape_library::get_palette() const -> const std::vector<Kit_palette_material>&
{
    return m_palette;
}

auto Kit_shape_library::find_palette_material(const std::string_view name) const -> const Kit_palette_material*
{
    for (const Kit_palette_material& material : m_palette) {
        if (material.name == name) {
            return &material;
        }
    }
    return nullptr;
}

auto Kit_shape_library::get_slot_palettes() const -> const std::vector<Kit_slot_palette>&
{
    return m_slot_palettes;
}

auto Kit_shape_library::find_slot_palette(const std::string_view slot) const -> const Kit_slot_palette*
{
    for (const Kit_slot_palette& palette : m_slot_palettes) {
        if (palette.slot == slot) {
            return &palette;
        }
    }
    return nullptr;
}

auto Kit_shape_library::find_slot_default(const std::string_view slot) const -> const Kit_palette_material*
{
    const Kit_slot_palette* const palette = find_slot_palette(slot);
    return (palette != nullptr) ? find_palette_material(palette->default_material) : nullptr;
}

} // namespace editor
