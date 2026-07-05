#pragma once

namespace editor {

// Canvas-safe stepper widgets shared by every graph editor.
//
// ImGui popups (Combo, BeginPopup) cannot be used inside the ax::NodeEditor
// canvas, so node content picks an enum / index with these left/right arrow
// steppers instead. They are payload-agnostic (ImGui only), so the geometry
// graph and the texture graph share this one pair rather than each carrying an
// identical copy (the copies used to be renamed apart to avoid an ODR clash -
// this header removes that hack).

// Left / right arrow buttons cycling index through [0, count).
auto imgui_index_stepper(const char* id, int& index, int count) -> bool;

// Index stepper followed by the current entry name.
auto imgui_enum_stepper(const char* id, int& index, const char* const* names, int count) -> bool;

} // namespace editor
