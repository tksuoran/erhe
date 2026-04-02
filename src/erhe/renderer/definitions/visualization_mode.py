from erhe_codegen import *

enum("Visualization_mode",
    value("off",      0, short_desc="Off"),
    value("selected", 1, short_desc="Selected"),
    value("all",      2, short_desc="All"),
    underlying_type=UInt,
)
