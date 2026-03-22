from erhe_codegen import *

enum("Light_type_serial",
    value("directional", 0),
    value("point",       1),
    value("spot",        2),
)

struct("Light_data",
    version=1,
    fields=[
        field("node_id",          UInt64,                        added_in=1, default="0", short_desc="Node this light is attached to"),
        field("name",             String,                        added_in=1, default='""'),
        field("type",             EnumRef("Light_type_serial"),  added_in=1, default="Light_type_serial::directional"),
        field("color",            Vec3,                          added_in=1, default="1.0f, 1.0f, 1.0f"),
        field("intensity",        Float,                         added_in=1, default="1.0f"),
        field("range",            Float,                         added_in=1, default="100.0f"),
        field("inner_spot_angle", Float,                         added_in=1, default="0.0f"),
        field("outer_spot_angle", Float,                         added_in=1, default="0.0f"),
        field("cast_shadow",      Bool,                          added_in=1, default="true"),
    ],
)
