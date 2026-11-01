from erhe_codegen import *

# How a headset controller select (right A) changes the selection
# (doc/editor/selection.md "Headset selection").
enum("Xr_selection_mode",
    value("single", 0, short_desc="Single (select the pointed object alone, or deselect it; 0 or 1 selected)"),
    value("toggle", 1, short_desc="Toggle (add or remove the pointed object; the selection accumulates)"),
    underlying_type=UInt,
)
