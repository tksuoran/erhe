from erhe_codegen import *

# Side of the target dock node a window is split off to in the procedural
# default layout (see editor_default_layout.cpp). "none" tabs the window into
# the target's dock node instead of splitting it.
enum("Dock_direction",
    value("none",  0, short_desc="Tab into the target's dock node"),
    value("left",  1, short_desc="Split off the left side of the target"),
    value("right", 2, short_desc="Split off the right side of the target"),
    value("up",    3, short_desc="Split off the top of the target"),
    value("down",  4, short_desc="Split off the bottom of the target"),
    underlying_type=UInt,
)
