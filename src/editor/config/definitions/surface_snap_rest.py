from erhe_codegen import *

# What snap to surface rests with Orient To Surface off, in Transform tool drags and Brush tool kit placement (U60,
# doc/editor/level_tools.md "Surface settings").
enum("Surface_snap_rest",
    value("leading_face", 0, short_desc="Leading face: the bounds face leading along the search direction rests flush"),
    value("anchor",       1, short_desc="Anchor: the anchor point (gizmo anchor, piece origin) lands on the surface"),
    underlying_type=UInt,
)
