#pragma once

namespace erhe::imgui {
    class Imgui_window;
    class Imgui_windows;
}

namespace editor {

// Placement helpers for dynamically created editor / properties windows
// (issue #258). A newly created window has no persisted ini position, so it
// would otherwise appear at ImGui's default cascade position. These helpers
// request an initial placement (applied on the window's next begin()):
//
//  - Editor windows (viewport / geometry graph / texture graph): dock (tab)
//    the new window with an existing editor window, preferring one of the same
//    concrete type, then any editor window. If none is currently docked, show
//    it floating, centered on the viewport, at 66% of the viewport size.
//
//  - Properties windows: dock (tab) with an existing Properties window. If
//    none is currently docked, show it floating, centered, at 33% width x 66%
//    height of the viewport.
//
//  - Hierarchy windows: dock (tab) with an existing scene-hierarchy window. If
//    none is currently docked, show it floating, centered, at 25% width x 50%
//    height of the viewport.
//
// new_window must already be registered in imgui_windows.

void apply_editor_window_placement    (erhe::imgui::Imgui_windows& imgui_windows, erhe::imgui::Imgui_window& new_window);
void apply_properties_window_placement(erhe::imgui::Imgui_windows& imgui_windows, erhe::imgui::Imgui_window& new_window);
void apply_hierarchy_window_placement (erhe::imgui::Imgui_windows& imgui_windows, erhe::imgui::Imgui_window& new_window);

} // namespace editor
