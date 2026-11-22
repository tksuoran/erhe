from erhe_codegen import *

# How a Paint tool Brush dab blends toward its target (doc/editor/tools.md "Channel painting").
enum("Paint_brush_blend",
    value("mix",      0, short_desc="Mix: toward the target"),
    value("add",      1, short_desc="Add: the target times alpha added"),
    value("subtract", 2, short_desc="Subtract: the target times alpha subtracted"),
    underlying_type=UInt,
)
