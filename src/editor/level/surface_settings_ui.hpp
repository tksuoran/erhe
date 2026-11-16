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
// The surface max distance in meters (U30).
void surface_max_distance_input   (Level_kits& level_kits, Surface_widget_label label);
// The connector snap distance in meters (D3.13), 0 = off.
void connector_snap_distance_input(Level_kits& level_kits, Surface_widget_label label);
// U27: on, an axis or plane drag applies only the snap offset's part along the dragged axis or in the plane.
void connector_snap_axis_checkbox (Level_kits& level_kits, Surface_widget_label label);

// Both checkboxes, the surface max distance, the connector snap distance and its axis checkbox with their labels (the Level Kit window).
void surface_settings_checkboxes(Level_kits& level_kits);

// Two toggle buttons, the surface max distance, the connector snap distance and its axis toggle of the viewport toolbar; text buttons without the icon font.
void surface_settings_toolbar_buttons(Level_kits& level_kits, ImFont* icon_font, float font_size);

} // namespace editor
