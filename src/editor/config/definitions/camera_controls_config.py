from erhe_codegen import *

struct("Camera_controls_config",
    version=1,
    short_desc="Camera Control",
    long_desc="",
    fields=[
        field(
            "invert_x",
            Bool,
            added_in=1,
            default="false",
            short_desc="Invert Mouse X Direction",
            long_desc="",
            visible=True
        ),
        field(
            "invert_y",
            Bool,
            added_in=1,
            default="false",
            short_desc="Invert Mouse Y Direction",
            long_desc="",
            visible=True
        ),
        field(
            "sensitivity",
            Float,
            added_in=1,
            default="1.0f",
            short_desc="Camera Sensitivity",
            long_desc="",
            visible=True
        ),
        field(
            "velocity_damp",
            Float,
            added_in=1,
            default="0.92f",
            short_desc="Velocity Damp",
            long_desc="",
            visible=True
        ),
        field(
            "velocity_max_delta",
            Float,
            added_in=1,
            default="0.004f",
            short_desc="Velocity Max Delta",
            long_desc="",
            visible=True
        ),
        field(
            "move_power",
            Float,
            added_in=1,
            default="1000.0f",
            short_desc="Move Power",
            long_desc="",
            visible=True
        ),
        field(
            "move_speed",
            Float,
            added_in=1,
            default="2.0f",
            short_desc="Move Speed",
            long_desc="",
            visible=True
        ),
        field(
            "turn_speed",
            Float,
            added_in=1,
            default="1.0f",
            short_desc="Turn Speed",
            long_desc="",
            visible=True
        ),
    ],
)
