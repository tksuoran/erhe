#pragma once

#include "level/kit_shape.hpp"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace editor {

class Kit_definition
{
public:
    std::string  name;
    std::string  display_name;
    unsigned int triangle_ceiling{0}; // per shape at default parameters (R2.6)
};

// A flat-color material of the kits' palette (R2.9, D3.4).
class Kit_palette_material
{
public:
    std::string name;
    glm::vec3   base_color{1.0f};
    float       roughness {0.8f};
    glm::vec3   emissive  {0.0f}; // linear, added to the shaded color; non-zero for a glowing material (P3)
};

// The palette materials of one slot name: its default and its alternatives, by palette material name.
class Kit_slot_palette
{
public:
    std::string              slot;
    std::string              default_material;
    std::vector<std::string> alternatives;
};

// Every kit and shape definition, built once (D1.5); adding a shape is one generator and one table entry.
class Kit_shape_library
{
public:
    Kit_shape_library();
    // Kit_parameter_values point at the definitions, so the table never moves.
    Kit_shape_library(const Kit_shape_library&) = delete;
    auto operator=(const Kit_shape_library&) -> Kit_shape_library& = delete;

    [[nodiscard]] auto get_kits  () const -> const std::vector<Kit_definition>&;
    [[nodiscard]] auto get_shapes() const -> const std::vector<Kit_shape_definition>&;
    [[nodiscard]] auto find      (std::string_view id) const -> const Kit_shape_definition*;
    [[nodiscard]] auto find_kit  (std::string_view name) const -> const Kit_definition*;
    [[nodiscard]] auto get_palette          () const -> const std::vector<Kit_palette_material>&;
    [[nodiscard]] auto find_palette_material(std::string_view name) const -> const Kit_palette_material*;
    [[nodiscard]] auto get_slot_palettes    () const -> const std::vector<Kit_slot_palette>&;
    [[nodiscard]] auto find_slot_palette    (std::string_view slot) const -> const Kit_slot_palette*;
    // The default palette material of the slot name; null for a slot without a palette.
    [[nodiscard]] auto find_slot_default    (std::string_view slot) const -> const Kit_palette_material*;

private:
    std::vector<Kit_definition>       m_kits;
    std::vector<Kit_shape_definition> m_shapes;
    std::vector<Kit_palette_material> m_palette;
    std::vector<Kit_slot_palette>     m_slot_palettes;
};

} // namespace editor
