#pragma once

#include <string>
#include <vector>

namespace editor {

// Canvas-safe dropdown / color widgets shared by every graph editor.
//
// These are real ImGui popups (BeginCombo, ColorEdit's picker) drawn directly
// inside ax::NodeEditor's BeginNode / EndNode. That works because of the issue
// #251 flip to native-resolution rendering (commit 908eb081): the vendored node
// editor now authors node content directly in SCREEN space at the zoomed size
// instead of laying out in a faked scaled-down local space, and no longer fakes
// io.MousePos / the viewport / window->Pos. A popup opened in a node therefore
// gets real screen coordinates and real mouse input. Before that flip popups
// were genuinely broken here, which is why node content used arrow steppers and
// a display-only swatch; the #251 Phase 5 pilot proved the fake space is gone,
// and this is the broad conversion it deferred.
//
// All of them take a `scale` multiplying their fixed pixel sizes; nodes pass
// Graph_editor_node::content_scale() (the view zoom), the properties panel 1.
// The node editor pushes a zoom-scaled font and style vars, so only hardcoded
// pixel constants need it - but they do need it (issue #251 Phase 6).

// Dropdown over a fixed name table. Returns true when a new index was picked.
//
// The preview is bounds-checked: a combo indexes names[index] directly, and the
// graph-file / MCP set_parameter paths can drive an enum out of range - the old
// arrow steppers clamped, so this was latent. An unchecked preview crashed the
// #251 Phase 5 pilot (0xc0000005, plain out-of-bounds read).
auto imgui_enum_combo(const char* id, int& index, const char* const* names, int count, float scale = 1.0f) -> bool;

// Same, for a runtime-built list (material / brush / mesh / scene pickers).
auto imgui_enum_combo(const char* id, int& index, const std::vector<std::string>& names, float scale = 1.0f) -> bool;

// Color swatch that opens ImGui's color picker (RGBA, alpha bar). Returns true
// on any frame the picker reports a new color, so dragging updates live.
auto imgui_color_edit(const char* id, float color[4], float scale = 1.0f) -> bool;

// Left / right arrow buttons cycling index through [0, count). Kept for the
// places where a dropdown is not wanted - a two-state toggle reads better as
// arrows than as a two-item list.
auto imgui_index_stepper(const char* id, int& index, int count) -> bool;

} // namespace editor
