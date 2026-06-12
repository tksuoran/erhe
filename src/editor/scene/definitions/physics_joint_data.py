from erhe_codegen import *

struct("Physics_joint_data",
    version=1,
    fields=[
        field("id",     UInt64,                             added_in=1, default="0",  short_desc="File-local id (1-based), referenced by Node_joint_data::joint_id"),
        field("name",   String,                             added_in=1, default='""'),
        field("limits", Vector(StructRef("Joint_limit_data")), added_in=1),
        field("drives", Vector(StructRef("Joint_drive_data")), added_in=1),
    ],
)
