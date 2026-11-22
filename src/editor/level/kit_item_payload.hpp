#pragma once

#include <cstdint>

namespace editor {

// ImGui drag-and-drop payload type of a kit item dragged out of the Level Kit window.
constexpr const char* c_kit_item_payload_type = "Kit_Item";

// Trivially copyable: the token names the kit item Level_kits holds for the drag (Level_kits::find_dragged_kit_item()).
class Kit_item_payload
{
public:
    std::uint64_t token{0};
};

} // namespace editor
