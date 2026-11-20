from erhe_codegen import *

# The search direction of a Transform tool drag's snap to surface (U51,
# doc/editor/level_tools.md "Surface settings").
enum("Surface_drag_direction",
    value("explicit_vector", 0, short_desc="Explicit: the Surface Drag Direction vector"),
    value("camera_ray",      1, short_desc="Camera ray: from the viewport camera through the anchor"),
    underlying_type=UInt,
)
