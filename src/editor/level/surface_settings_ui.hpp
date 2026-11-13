#pragma once

struct ImFont;

namespace editor {

class Level_kits;

// The widgets of the shared surface pair (doc/editor/level_tools.md "Surface settings").
// Each writes through Level_kits at the widget's change.

enum class Surface_widget_label : unsigned int {
    shown = 0,
    hidden    // a Property_editor row names the widget
};

void surface_snap_checkbox  (Level_kits& level_kits, Surface_widget_label label);
void surface_orient_checkbox(Level_kits& level_kits, Surface_widget_label label);

// Both checkboxes with their labels (the Level Kit window).
void surface_settings_checkboxes(Level_kits& level_kits);

// Two toggle buttons of the viewport toolbar; text buttons without the icon font.
void surface_settings_toolbar_buttons(Level_kits& level_kits, ImFont* icon_font, float font_size);

} // namespace editor
