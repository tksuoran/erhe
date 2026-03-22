from erhe_codegen import *

struct("Collision_shape_data",
    version=1,
    fields=[
        field("type",         EnumRef("Collision_shape_type_serial"),    added_in=1, default="Collision_shape_type_serial::e_not_specified"),
        field("half_extents", Optional(Vec3),                           added_in=1, short_desc="For box/cylinder"),
        field("radius",       Optional(Float),                          added_in=1, short_desc="For sphere/capsule"),
        field("axis",         Optional(Int),                            added_in=1, short_desc="0=X, 1=Y, 2=Z"),
        field("length",       Optional(Float),                          added_in=1, short_desc="For capsule"),
        field("children",     Vector(StructRef("Compound_child_data")), added_in=1, short_desc="For compound shapes"),
    ],
)
