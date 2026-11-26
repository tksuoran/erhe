#include "level/kit_shape_library.hpp"
#include "level/kit_shapes.hpp"

#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>
#include <vector>

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

void add_former_build(std::vector<Kit_shape_definition>& shapes, const std::string_view id, Kit_generator former_generator)
{
    const std::vector<Kit_shape_definition>::const_iterator current = std::find_if(
        shapes.begin(), shapes.end(), [id](const Kit_shape_definition& shape) { return shape.id == id; }
    );
    ERHE_VERIFY(current != shapes.end());
    Kit_shape_definition former = *current;
    former.id           = fmt::format("{}_old", id);
    former.display_name = fmt::format("{} (old)", current->display_name);
    former.description  = fmt::format("The former build of {}, kept for comparison. {}", id, current->description);
    former.former_ids.clear();
    former.foliage_radius.reset();
    former.generator    = std::move(former_generator);
    shapes.push_back(std::move(former));
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

auto make_angle_parameter(const char* name, const char* description, const float default_degrees, const float min_degrees, const float max_degrees) -> Kit_parameter
{
    Kit_parameter parameter;
    parameter.name          = name;
    parameter.description   = description;
    parameter.type          = Kit_parameter_type::float_value;
    parameter.unit          = Kit_unit::degrees;
    parameter.default_scale = Kit_default_scale::meters;
    parameter.float_default = default_degrees;
    parameter.float_min     = min_degrees;
    parameter.float_max     = max_degrees;
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

auto make_detail_parameter() -> Kit_parameter
{
    Kit_parameter parameter;
    parameter.name          = c_kit_detail_parameter;
    parameter.description   = "Level of detail: the default is the shape's own, lower values fewer triangles, higher more";
    parameter.type          = Kit_parameter_type::float_value;
    parameter.unit          = Kit_unit::ratio;
    parameter.default_scale = Kit_default_scale::meters;
    parameter.float_default = c_kit_detail_default;
    parameter.float_min     = c_kit_detail_min;
    parameter.float_max     = 1.0f;
    parameter.text_presence = Kit_text_presence::unless_default;
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
        Kit_definition{.name = "basic",        .display_name = "Basic",        .triangle_ceiling = 2000},
        Kit_definition{.name = "architecture", .display_name = "Architecture", .triangle_ceiling = 2000},
        Kit_definition{.name = "nature",       .display_name = "Nature",       .triangle_ceiling = 2000},
        Kit_definition{.name = "prop",         .display_name = "Props",        .triangle_ceiling = 2000},
        Kit_definition{.name = "ruin",         .display_name = "Ruins",        .triangle_ceiling = 2000},
        Kit_definition{.name = "traversal",    .display_name = "Traversal",    .triangle_ceiling = 2000},
        Kit_definition{.name = "harbour",      .display_name = "Harbour",      .triangle_ceiling = 2000},
        Kit_definition{.name = "light",        .display_name = "Lights",       .triangle_ceiling = 2000},
        Kit_definition{.name = "industrial",   .display_name = "Industrial",   .triangle_ceiling = 2000},
        Kit_definition{.name = "machine",      .display_name = "Machine Elements", .triangle_ceiling = 2000}
    };
    add_basic_shapes           (m_shapes);
    add_architecture_shapes    (m_shapes);
    add_nature_shapes          (m_shapes);
    add_build_13_16_shapes     (m_shapes);
    add_build_17_20_shapes     (m_shapes);
    add_build_21_24_shapes     (m_shapes);
    add_extra_01_08_shapes     (m_shapes);
    add_extra_09_16_shapes     (m_shapes);
    add_extra_17_24_shapes     (m_shapes);
    add_extra_25_32_shapes     (m_shapes);
    add_extra_33_36_shapes     (m_shapes);
    add_machine_01_08_shapes   (m_shapes);
    add_machine_09_18_shapes   (m_shapes);
    // Every shape takes the level of detail as its last parameter. A shape with detail counts builds from the counts
    // it scales, and every callback describing its mesh takes the same values.
    for (Kit_shape_definition& shape : m_shapes) {
        ERHE_VERIFY(!shape.find_parameter(c_kit_detail_parameter).has_value());
        shape.parameters.push_back(make_detail_parameter());
        if (shape.detail_counts.empty()) {
            continue;
        }
        shape.generator = [f = std::move(shape.generator)](const Kit_parameter_values& values, GEO::Mesh& mesh) {
            f(get_kit_detail_values(values), mesh);
        };
        if (shape.check_walkability) {
            shape.check_walkability = [f = std::move(shape.check_walkability)](const Kit_parameter_values& values, const Kit_agent& agent) {
                return f(get_kit_detail_values(values), agent);
            };
        }
        if (shape.get_seam_sides) {
            shape.get_seam_sides = [f = std::move(shape.get_seam_sides)](const Kit_parameter_values& values) {
                return f(get_kit_detail_values(values));
            };
        }
        if (shape.get_connectors) {
            shape.get_connectors = [f = std::move(shape.get_connectors)](const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors) {
                f(get_kit_detail_values(values), out_connectors);
            };
        }
        if (shape.get_light_frames) {
            shape.get_light_frames = [f = std::move(shape.get_light_frames)](const Kit_parameter_values& values, std::vector<Kit_light_frame>& out_frames) {
                f(get_kit_detail_values(values), out_frames);
            };
        }
        if (shape.get_collision) {
            shape.get_collision = [f = std::move(shape.get_collision)](const Kit_parameter_values& values) {
                return f(get_kit_detail_values(values));
            };
        }
    }
    // Every id and former id names one shape.
    std::vector<std::string_view> names;
    for (const Kit_shape_definition& shape : m_shapes) {
        names.push_back(shape.id);
        names.insert(names.end(), shape.former_ids.begin(), shape.former_ids.end());
    }
    std::sort(names.begin(), names.end());
    ERHE_VERIFY(std::adjacent_find(names.begin(), names.end()) == names.end());
    for (const Kit_shape_definition& shape : m_shapes) {
        if (shape.foliage_radius.has_value()) {
            ERHE_VERIFY(std::isfinite(shape.foliage_radius.value()) && (shape.foliage_radius.value() > 0.0f));
            m_foliage_shapes.push_back(&shape);
        }
    }
    // Base colors are linear.
    m_palette = {
        Kit_palette_material{.name = "Kit Stone",          .base_color = glm::vec3{0.50f, 0.50f, 0.47f}, .roughness = 0.85f},
        Kit_palette_material{.name = "Kit Floor",          .base_color = glm::vec3{0.42f, 0.34f, 0.26f}, .roughness = 0.75f},
        Kit_palette_material{.name = "Kit Plaster",        .base_color = glm::vec3{0.78f, 0.72f, 0.60f}, .roughness = 0.90f},
        Kit_palette_material{.name = "Kit Trim",           .base_color = glm::vec3{0.60f, 0.56f, 0.50f}, .roughness = 0.80f},
        Kit_palette_material{.name = "Kit Wood",           .base_color = glm::vec3{0.45f, 0.29f, 0.15f}, .roughness = 0.70f},
        Kit_palette_material{.name = "Kit Dark Wood",      .base_color = glm::vec3{0.16f, 0.09f, 0.05f}, .roughness = 0.70f},
        Kit_palette_material{.name = "Kit Vermilion",      .base_color = glm::vec3{0.72f, 0.07f, 0.03f}, .roughness = 0.55f},
        Kit_palette_material{.name = "Kit Rock",           .base_color = glm::vec3{0.36f, 0.35f, 0.34f}, .roughness = 0.95f},
        Kit_palette_material{.name = "Kit Earth",          .base_color = glm::vec3{0.36f, 0.24f, 0.14f}, .roughness = 0.95f},
        Kit_palette_material{.name = "Kit Grass",          .base_color = glm::vec3{0.22f, 0.42f, 0.12f}, .roughness = 0.90f},
        Kit_palette_material{.name = "Kit Foliage",        .base_color = glm::vec3{0.10f, 0.32f, 0.08f}, .roughness = 0.85f},
        Kit_palette_material{.name = "Kit Autumn Foliage", .base_color = glm::vec3{0.48f, 0.22f, 0.05f}, .roughness = 0.85f},
        Kit_palette_material{.name = "Kit Bark",           .base_color = glm::vec3{0.22f, 0.15f, 0.09f}, .roughness = 0.95f},
        Kit_palette_material{.name = "Kit Dark Rock",      .base_color = glm::vec3{0.13f, 0.12f, 0.11f}, .roughness = 0.95f},
        Kit_palette_material{.name = "Kit Sand",           .base_color = glm::vec3{0.76f, 0.62f, 0.40f}, .roughness = 0.95f},
        Kit_palette_material{.name = "Kit Shell",          .base_color = glm::vec3{0.85f, 0.78f, 0.70f}, .roughness = 0.60f},
        Kit_palette_material{.name = "Kit Roof Tile",      .base_color = glm::vec3{0.52f, 0.20f, 0.12f}, .roughness = 0.80f},
        Kit_palette_material{.name = "Kit Water",          .base_color = glm::vec3{0.04f, 0.18f, 0.30f}, .roughness = 0.10f},
        Kit_palette_material{.name = "Kit Shallow Water",  .base_color = glm::vec3{0.10f, 0.40f, 0.42f}, .roughness = 0.10f},
        Kit_palette_material{.name = "Kit Straw",          .base_color = glm::vec3{0.66f, 0.55f, 0.30f}, .roughness = 0.90f},
        Kit_palette_material{.name = "Kit Bamboo",         .base_color = glm::vec3{0.30f, 0.42f, 0.10f}, .roughness = 0.55f},
        Kit_palette_material{.name = "Kit Golden Bamboo",  .base_color = glm::vec3{0.68f, 0.52f, 0.16f}, .roughness = 0.55f},
        Kit_palette_material{.name = "Kit Pale Wood",      .base_color = glm::vec3{0.62f, 0.47f, 0.28f}, .roughness = 0.80f},
        Kit_palette_material{.name = "Kit Coral",          .base_color = glm::vec3{0.80f, 0.30f, 0.18f}, .roughness = 0.70f},
        Kit_palette_material{.name = "Kit Glow",           .base_color = glm::vec3{1.00f, 0.72f, 0.36f}, .roughness = 0.40f, .emissive = glm::vec3{2.20f, 1.20f, 0.35f}},
        Kit_palette_material{.name = "Kit Metal",          .base_color = glm::vec3{0.22f, 0.23f, 0.25f}, .roughness = 0.50f, .metallic = 0.6f},
        Kit_palette_material{.name = "Kit Rust",           .base_color = glm::vec3{0.32f, 0.13f, 0.06f}, .roughness = 0.85f, .metallic = 0.4f},
        Kit_palette_material{.name = "Kit Cloth",          .base_color = glm::vec3{0.70f, 0.64f, 0.52f}, .roughness = 0.95f},
        Kit_palette_material{.name = "Kit Red Cloth",      .base_color = glm::vec3{0.55f, 0.07f, 0.06f}, .roughness = 0.95f},
        Kit_palette_material{.name = "Kit Blue Cloth",     .base_color = glm::vec3{0.08f, 0.17f, 0.42f}, .roughness = 0.95f},
        Kit_palette_material{.name = "Kit Clay",           .base_color = glm::vec3{0.58f, 0.27f, 0.15f}, .roughness = 0.80f},
        Kit_palette_material{.name = "Kit Ember",          .base_color = glm::vec3{0.60f, 0.18f, 0.05f}, .roughness = 0.60f, .emissive = glm::vec3{1.60f, 0.45f, 0.08f}},
        Kit_palette_material{.name = "Kit Steel",          .base_color = glm::vec3{0.70f, 0.72f, 0.75f}, .roughness = 0.35f, .metallic = 0.6f},
        Kit_palette_material{.name = "Kit Brass",          .base_color = glm::vec3{0.78f, 0.58f, 0.22f}, .roughness = 0.35f, .metallic = 0.6f},
        // Leaves (doc/erhe/leaf_material.md): a base color of the opaque counterpart / (1 - transmission), so
        // they reflect as much as it does; thicknesses over the shapes' thickest leaf (palm fronds and
        // banana blades about 5 cm, grass blades about 3.6 cm).
        Kit_palette_material{.name = "Kit Leaf",           .base_color = glm::vec3{0.17f, 0.53f, 0.13f}, .roughness = 0.60f, .transmission = 0.40f, .transmission_color = glm::vec3{0.45f, 0.75f, 0.12f}, .thickness = 0.06f},
        Kit_palette_material{.name = "Kit Young Leaf",     .base_color = glm::vec3{0.40f, 0.70f, 0.18f}, .roughness = 0.55f, .transmission = 0.45f, .transmission_color = glm::vec3{0.60f, 0.85f, 0.20f}, .thickness = 0.06f},
        Kit_palette_material{.name = "Kit Autumn Leaf",    .base_color = glm::vec3{0.80f, 0.37f, 0.08f}, .roughness = 0.70f, .transmission = 0.40f, .transmission_color = glm::vec3{0.95f, 0.55f, 0.12f}, .thickness = 0.06f},
        Kit_palette_material{.name = "Kit Grass Blade",    .base_color = glm::vec3{0.34f, 0.65f, 0.18f}, .roughness = 0.70f, .transmission = 0.35f, .transmission_color = glm::vec3{0.55f, 0.80f, 0.18f}, .thickness = 0.04f}
    };
    // One default per slot name and a few alternatives (R2.9).
    m_slot_palettes = {
        Kit_slot_palette{.slot = "stone",   .default_material = "Kit Stone",   .alternatives = {"Kit Rock", "Kit Plaster", "Kit Wood", "Kit Shell", "Kit Clay"}},
        Kit_slot_palette{.slot = "floor",   .default_material = "Kit Floor",   .alternatives = {"Kit Wood", "Kit Stone", "Kit Earth"}},
        Kit_slot_palette{.slot = "wall",    .default_material = "Kit Plaster", .alternatives = {"Kit Stone", "Kit Wood", "Kit Trim"}},
        Kit_slot_palette{.slot = "trim",    .default_material = "Kit Trim",    .alternatives = {"Kit Wood", "Kit Stone", "Kit Plaster"}},
        Kit_slot_palette{.slot = "rock",    .default_material = "Kit Rock",    .alternatives = {"Kit Stone", "Kit Earth"}},
        Kit_slot_palette{.slot = "earth",   .default_material = "Kit Earth",   .alternatives = {"Kit Grass", "Kit Rock", "Kit Stone", "Kit Sand"}},
        Kit_slot_palette{.slot = "grass",   .default_material = "Kit Grass",   .alternatives = {"Kit Earth", "Kit Foliage"}},
        Kit_slot_palette{.slot = "foliage", .default_material = "Kit Foliage", .alternatives = {"Kit Autumn Foliage", "Kit Grass"}},
        // Thin leaves and blades (the leaf BxDF); "foliage" stays the closed clumps of trees and bushes.
        Kit_slot_palette{.slot = "leaf",       .default_material = "Kit Leaf",        .alternatives = {"Kit Young Leaf", "Kit Autumn Leaf", "Kit Foliage"}},
        Kit_slot_palette{.slot = "young_leaf", .default_material = "Kit Young Leaf",  .alternatives = {"Kit Leaf", "Kit Autumn Leaf", "Kit Grass"}},
        Kit_slot_palette{.slot = "blade",      .default_material = "Kit Grass Blade", .alternatives = {"Kit Leaf", "Kit Autumn Leaf", "Kit Grass", "Kit Straw"}},
        Kit_slot_palette{.slot = "bark",    .default_material = "Kit Bark",    .alternatives = {"Kit Wood", "Kit Rock"}},
        Kit_slot_palette{.slot = "cliff",   .default_material = "Kit Dark Rock", .alternatives = {"Kit Rock", "Kit Stone", "Kit Sand"}},
        Kit_slot_palette{.slot = "sand",    .default_material = "Kit Sand",    .alternatives = {"Kit Earth", "Kit Rock"}},
        Kit_slot_palette{.slot = "roof",    .default_material = "Kit Roof Tile", .alternatives = {"Kit Wood", "Kit Stone"}},
        Kit_slot_palette{.slot = "water",   .default_material = "Kit Water",   .alternatives = {"Kit Shallow Water"}},
        Kit_slot_palette{.slot = "wood",    .default_material = "Kit Wood",    .alternatives = {"Kit Vermilion", "Kit Dark Wood", "Kit Bark"}},
        Kit_slot_palette{.slot = "shoot",   .default_material = "Kit Grass",   .alternatives = {"Kit Foliage", "Kit Autumn Foliage"}},
        Kit_slot_palette{.slot = "plume",   .default_material = "Kit Straw",   .alternatives = {"Kit Shell", "Kit Bark", "Kit Autumn Foliage"}},
        Kit_slot_palette{.slot = "cane",    .default_material = "Kit Bamboo",  .alternatives = {"Kit Golden Bamboo", "Kit Straw", "Kit Dark Wood"}},
        Kit_slot_palette{.slot = "sapwood",   .default_material = "Kit Pale Wood", .alternatives = {"Kit Wood", "Kit Straw", "Kit Shell"}},
        Kit_slot_palette{.slot = "heartwood", .default_material = "Kit Wood",      .alternatives = {"Kit Dark Wood", "Kit Bark", "Kit Pale Wood"}},
        Kit_slot_palette{.slot = "shell",     .default_material = "Kit Shell",     .alternatives = {"Kit Sand", "Kit Stone", "Kit Coral"}},
        Kit_slot_palette{.slot = "coral",     .default_material = "Kit Coral",     .alternatives = {"Kit Shell", "Kit Vermilion", "Kit Sand"}},
        Kit_slot_palette{.slot = "glow",      .default_material = "Kit Glow",      .alternatives = {"Kit Ember"}},
        Kit_slot_palette{.slot = "metal",     .default_material = "Kit Metal",     .alternatives = {"Kit Rust", "Kit Dark Wood", "Kit Stone", "Kit Steel", "Kit Brass"}},
        Kit_slot_palette{.slot = "cloth",     .default_material = "Kit Cloth",     .alternatives = {"Kit Red Cloth", "Kit Blue Cloth", "Kit Straw"}},
        Kit_slot_palette{.slot = "steel",     .default_material = "Kit Steel",     .alternatives = {"Kit Brass", "Kit Metal", "Kit Rust"}}
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

auto Kit_shape_library::get_foliage_shapes() const -> const std::vector<const Kit_shape_definition*>&
{
    return m_foliage_shapes;
}

auto Kit_shape_library::find(const std::string_view id) const -> const Kit_shape_definition*
{
    for (const Kit_shape_definition& shape : m_shapes) {
        if (shape.id == id) {
            return &shape;
        }
        for (const std::string& former_id : shape.former_ids) {
            if (former_id == id) {
                return &shape;
            }
        }
    }
    return nullptr;
}

auto read_kit_piece_record(
    const Kit_shape_library& library,
    const std::string_view   shape_id,
    const std::string_view   parameters,
    Kit_parameter_values&    out_values
) -> std::string
{
    const Kit_shape_definition* const shape = library.find(shape_id);
    if (shape == nullptr) {
        return fmt::format("the kits have no shape '{}'", shape_id);
    }
    out_values = Kit_parameter_values{*shape};
    const std::string parse_error = parse_canonical_text(*shape, parameters, out_values);
    if (!parse_error.empty()) {
        return fmt::format("the parameters do not read: {}", parse_error);
    }
    return out_values.validate();
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

auto Kit_shape_library::get_triangle_ceiling(const Kit_parameter_values& values) const -> unsigned int
{
    const Kit_shape_definition* const shape = values.get_shape();
    const Kit_definition* const       kit   = (shape != nullptr) ? find_kit(shape->kit) : nullptr;
    if (kit == nullptr) {
        return 0;
    }
    const float scale = shape->get_ceiling_scale ? std::max(shape->get_ceiling_scale(values), 1.0f) : 1.0f;
    return static_cast<unsigned int>(static_cast<float>(kit->triangle_ceiling) * scale);
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
