from erhe_codegen import *

struct("Camera_controls_config",
    field("invert_x",           Bool,  added_in=1, default="false"),
    field("invert_y",           Bool,  added_in=1, default="false"),
    field("sensitivity",        Float, added_in=1, default="1.0f"),
    field("velocity_damp",      Float, added_in=1, default="0.92f"),
    field("velocity_max_delta", Float, added_in=1, default="0.004f"),
    field("move_power",         Float, added_in=1, default="1000.0f"),
    field("move_speed",         Float, added_in=1, default="2.0f"),
    field("turn_speed",         Float, added_in=1, default="1.0f"),
    version=1,
)
