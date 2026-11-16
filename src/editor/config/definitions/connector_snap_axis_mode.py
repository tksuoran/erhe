from erhe_codegen import *

# How an axis or plane drag of the Transform tool applies the connector snap offset
# (doc/editor/level_tools.md "Connector snap").
enum("Connector_snap_axis_mode",
    value("full_offset",  0, short_desc="Full offset: the pair co-locates"),
    value("stay_on_axis", 1, short_desc="Stay on the dragged axis or plane"),
    underlying_type=UInt,
)
