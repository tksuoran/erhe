from erhe_codegen import *

struct("Camera_data",
    version=1,
    fields=[
        field("node_id",      UInt64,                      added_in=1, default="0",    short_desc="Node this camera is attached to"),
        field("name",         String,                      added_in=1, default='""'),
        field("projection",   StructRef("Projection_data"), added_in=1),
        field("exposure",     Float,                       added_in=1, default="1.0f"),
        field("shadow_range", Float,                       added_in=1, default="22.0f"),
    ],
)
