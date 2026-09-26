from erhe_codegen import *

# Which joint constraints the Debug Visualizations draw
# (doc/editor/tools.md "Debug_visualizations"): none, every constraint of
# the scene, the constraints that move the hovered mesh, or those of the
# hovered bone.
enum("Joint_constraint_filter",
    value("off",          0, short_desc="Off"),
    value("all",          1, short_desc="All constraints"),
    value("hovered_mesh", 2, short_desc="Constraints of hovered mesh"),
    value("hovered_bone", 3, short_desc="Constraints of hovered bone"),
    underlying_type=UInt,
)
