from erhe_codegen import *

# Controls what a zoom glide does when the camera turns while the glide runs:
# 'keep_world' keeps the world direction captured at the wheel step;
# 'follow_view' keeps the direction relative to the view, so it turns with the
# camera.
enum("Zoom_glide_direction",
    value("keep_world",  0, short_desc="Keep world direction"),
    value("follow_view", 1, short_desc="Follow view"),
    underlying_type=UInt,
)
