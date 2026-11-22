#pragma once

#include "level/kit_item.hpp"

#include <optional>
#include <string>

struct Inventory_slot;

namespace editor {

class Kit_shape_library;

// What a saved inventory slot's kit fields read as (doc/editor/level_tools.md "Kit slots").
class Kit_item_slot_reading
{
public:
    std::optional<Kit_item> item;    // none for a slot without a kit shape, or when the fields do not read
    std::string             problem; // why the fields do not read; empty otherwise
};

// The kit item of the slot's kit_shape (or a former id), kit_parameters and kit_materials.
[[nodiscard]] auto read_kit_item_slot(const Kit_shape_library& library, const Inventory_slot& slot) -> Kit_item_slot_reading;
// Writes the item's shape id, canonical parameter text and slot materials into the slot's kit fields.
void write_kit_item_slot(const Kit_item& item, Inventory_slot& out_slot);
// The tooltip of a kit slot: display name, id, the parameter values and the slot materials. Allocates.
[[nodiscard]] auto make_kit_item_tooltip(const Kit_item& item) -> std::string;

} // namespace editor
