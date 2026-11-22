from erhe_codegen import *

# What a Paint tool Brush stroke paints (doc/editor/tools.md "Paint tool brush").
enum("Paint_brush_target",
    value("weight", 0, short_desc="Weight: one channel toward the target value"),
    value("color",  1, short_desc="Color: all four channels toward the selected palette color"),
    underlying_type=UInt,
)
