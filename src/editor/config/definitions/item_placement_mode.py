from erhe_codegen import *

# Where Create and Paste of a viewport's context menu put an item relative to
# the hovered point (doc/editor/windows.md "Viewport context menu").
enum("Item_placement_mode",
    value("origin",             0, short_desc="Origin (the item's origin on the hovered point)"),
    value("avoid_intersection", 1, short_desc="Avoid Intersection (moved along the surface normal until the item is clear of the surface)"),
    underlying_type=UInt,
)
