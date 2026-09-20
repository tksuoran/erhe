from erhe_codegen import *

# Controls what the zoom input (mouse wheel) does in a view with a perspective
# camera: 'view_axis' moves the camera along its view axis; 'hover_point' moves
# the camera along the axis from the camera position towards the point under the
# pointer, so that point keeps its screen position.
enum("Perspective_zoom_mode",
    value("view_axis",   0, short_desc="Along view axis"),
    value("hover_point", 1, short_desc="Towards hovered point"),
    underlying_type=UInt,
)
