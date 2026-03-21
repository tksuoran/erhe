from erhe_codegen import *

struct("Light",
    field("name",      String,               added_in=1, default='""', short_desc="Light name"),
    field("color",     StructRef("Color"),    added_in=1, short_desc="Light color"),
    field("intensity", Float,                 added_in=1, default="1.0f", short_desc="Intensity"),
    field("shadows",   Bool,                  added_in=2, default="false", short_desc="Cast shadows"),
    field("colors",    Vector(StructRef("Color")), added_in=2, short_desc="Additional colors"),
    field("falloff",   Optional(StructRef("Color")), added_in=2, short_desc="Falloff color"),
    version=2,
)
