from erhe_codegen import *

struct("Compound_child_data",
    version=1,
    fields=[
        field("shape_type",   EnumRef("Collision_shape_type_serial"), added_in=1, default="Collision_shape_type_serial::e_empty"),
        field("half_extents", Optional(Vec3),                        added_in=1, short_desc="For box/cylinder"),
        field("radius",       Optional(Float),                       added_in=1, short_desc="For sphere/capsule"),
        field("axis",         Optional(Int),                         added_in=1, short_desc="0=X, 1=Y, 2=Z"),
        field("length",       Optional(Float),                       added_in=1, short_desc="For capsule"),
        field("position",     Vec3,                                  added_in=1, default="0.0f, 0.0f, 0.0f"),
        field("rotation",     Vec4,                                  added_in=1, default="0.0f, 0.0f, 0.0f, 1.0f", short_desc="Quaternion xyzw"),
    ],
)
