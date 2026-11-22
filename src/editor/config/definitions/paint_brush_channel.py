from erhe_codegen import *

# The vertex color channel a Paint tool Weight stroke paints on a material with detail blend off.
enum("Paint_brush_channel",
    value("r", 0, short_desc="Red"),
    value("g", 1, short_desc="Green"),
    value("b", 2, short_desc="Blue"),
    value("a", 3, short_desc="Alpha"),
    underlying_type=UInt,
)
