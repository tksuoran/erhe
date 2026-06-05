#pragma once

namespace erhe::primitive { class Material; }

namespace editor {

class Brush;
class Tool;

// ImGui drag-and-drop payload type string for inventory slot drags
constexpr const char* c_inventory_slot_payload_type = "Inventory_Slot";

// POD payload for drag-and-drop (safe for memcpy by ImGui)
class Slot_drag_payload
{
public:
    enum class Section : int { palette = 0, grid = 1, hotbar = 2 };

    Tool*                      tool    {nullptr};
    Brush*                     brush   {nullptr};
    erhe::primitive::Material* material{nullptr};
    Section                    section {Section::palette};
    int                        index   {-1};
};

}
