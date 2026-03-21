from erhe_codegen import *

struct("Grid_config",
    field("snap_enabled", Bool,  added_in=1, default="true"),
    field("visible",      Bool,  added_in=1, default="true"),
    field("major_color",  Vec4,  added_in=1, default="1.0f, 1.0f, 1.0f, 1.0f"),
    field("minor_color",  Vec4,  added_in=1, default="0.5f, 0.5f, 0.5f, 0.5f"),
    field("major_width",  Float, added_in=1, default="4.0f"),
    field("minor_width",  Float, added_in=1, default="2.0f"),
    field("cell_size",    Float, added_in=1, default="1.0f"),
    field("cell_div",     Int,   added_in=1, default="10"),
    field("cell_count",   Int,   added_in=1, default="2"),
    version=1,
)
