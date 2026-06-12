from erhe_codegen import *

struct("Joint_drive_data",
    version=1,
    fields=[
        field("type",            EnumRef("Drive_type_serial"), added_in=1, default="Drive_type_serial::e_linear"),
        field("mode",            EnumRef("Drive_mode_serial"), added_in=1, default="Drive_mode_serial::e_force"),
        field("axis",            Int,                          added_in=1, default="0",    short_desc="0=X, 1=Y, 2=Z"),
        field("max_force",       Optional(Float),              added_in=1,                 short_desc="Absent = unlimited"),
        field("position_target", Float,                        added_in=1, default="0.0f"),
        field("velocity_target", Float,                        added_in=1, default="0.0f"),
        field("stiffness",       Float,                        added_in=1, default="0.0f", short_desc="> 0 selects a position motor"),
        field("damping",         Float,                        added_in=1, default="0.0f"),
    ],
)
