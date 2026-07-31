from erhe_codegen import *

# Controls where the rotation protractor's swept sector is anchored during a
# rotation drag: 'drag_relative' anchors the initial spoke to the pointer
# position at drag start (the sector always starts where the drag started);
# 'axis_absolute' anchors the protractor to the active coordinate space, so
# the initial and current spokes sit at the anchor's absolute initial and
# current twist angles around the rotation axis. The drag delta amount is
# computed the same way in both modes.
enum("Rotate_sector_anchoring",
    value("drag_relative", 0, short_desc="Drag Relative"),
    value("axis_absolute", 1, short_desc="Axis Absolute"),
    underlying_type=UInt,
)
