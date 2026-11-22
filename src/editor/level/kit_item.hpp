#pragma once

#include "level/kit_shape.hpp"

#include <span>
#include <string>
#include <vector>

namespace editor {

// A kit shape with its parameter values and one palette material name per slot: what a kit mode click places
// (doc/editor/level_tools.md "Dragging a kit item").
class Kit_item
{
public:
    Kit_item() = default;
    // A missing or empty slot name takes the slot's default when the item is placed.
    Kit_item(const Kit_parameter_values& values, std::span<const std::string> slot_materials);

    [[nodiscard]] auto get_shape         () const -> const Kit_shape_definition*;
    [[nodiscard]] auto get_values        () const -> const Kit_parameter_values&;
    [[nodiscard]] auto get_slot_materials() const -> const std::vector<std::string>&;
    // The shape's display name; empty without a shape.
    [[nodiscard]] auto get_display_name  () const -> const std::string&;
    // Empty when the item has a shape and its values validate; otherwise the reason.
    [[nodiscard]] auto validate          () const -> std::string;
    [[nodiscard]] auto is_valid          () const -> bool;

private:
    Kit_parameter_values     m_values;
    std::vector<std::string> m_slot_materials;
};

} // namespace editor
